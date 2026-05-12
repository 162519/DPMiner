#ifndef PMINER_H
#define PMINER_H
#include <iostream>
#include <tbb/tbb.h>
#include <chrono>
#include <ctime>
#include <sys/time.h>
#include"global_degree.h"
#include "Pattern.h"
#include "Degree.h"
#include "../util/global.h"
#include "../util/timer.h"
#include "Request.h"
#include "Response.h"
#include "Task.h"
using namespace std;
using namespace tbb;

class PMiner {

private:

    Pattern *p;
    Degree *degree_P;
    std::vector<Degree_R> degree_R;
    int ThreadNum;
    task_group tg;
    vector<unsigned> minMatchID_PMR;

    //分布式
    Request req;
    Response resp;
    global_degree gd;

    alignas(64) std::atomic<unsigned long long> finalAns=0;
    alignas(64) std::atomic<int> inFlightTasks_{0};
    double precache_ratio_;
    ThreadSlot* computeThreadSlot_ = nullptr;
    ThreadSlot* mainThreadSlot_ = nullptr;
    ThreadSlot* prefetchThreadSlot_ = nullptr;

    // 多级环形流水线
    static constexpr int PIPELINE_DEPTH = 4;
    PrefetchBatch prefetchBufs_[PIPELINE_DEPTH];
    std::atomic<bool> slotReady_[PIPELINE_DEPTH]{{},{},{},{}};
    std::atomic<bool> slotConsumed_[PIPELINE_DEPTH]{true,true,true,true};
    std::atomic<bool> prefetchDone_{false};
    std::mutex slotMtx_;
    std::condition_variable slotCv_;
    //直接判断标签是否一致
    bool islabelEqual(vector<int> *label1, vector<int> *label2)
    {
        if(label1 == NULL && label2 == NULL) return true;
        if (label1->size() == 0 && label2->size() == 0) return true;
        if (label1->size() != label2->size())
        {
            return false;
        }
        for (int i = 0; i < label1->size(); ++i)
        {
            if (label1->at(i) != label2->at(i))
            {
                return false;
            }
        }
        return true;
    }

    void precacheHighDegreeVertices() {
        if (precache_ratio_ <= 0.0) return;

        vector<pair<unsigned, unsigned>> remoteVertices;
        unsigned num_v = g->getnum_v();
        for (unsigned vid = 0; vid < num_v; ++vid) {
            if (!g->isLocal(vid)) {
                remoteVertices.emplace_back(g->getR_deg(vid), vid);
            }
        }
        sort(remoteVertices.begin(), remoteVertices.end(),
             [](const auto& a, const auto& b) { return a.first > b.first; });

        size_t precacheCount = static_cast<size_t>(remoteVertices.size() * precache_ratio_);
        if (precacheCount == 0) return;
        precacheCount = min(precacheCount, remoteVertices.size());

        vector<unsigned> precacheVids;
        precacheVids.reserve(precacheCount);
        for (size_t i = 0; i < precacheCount; ++i) {
            unsigned vid = remoteVertices[i].second;
            if (requestedVids.count(vid) == 0) {
                bitset<8> nodeList = bitmap->getNodeList(vid);
                bool hasOwner = false;
                for (int j = 0; j < WORKER_NUM; ++j) {
                    if (j != my_rank && nodeList.test(j)) {
                        hasOwner = true;
                        break;
                    }
                }
                if (hasOwner) {
                    precacheVids.push_back(vid);
                }
            }
        }

        if (precacheVids.empty()) return;

        printf("[rank %d] precache: top %zu/%zu remote vertices (ratio=%.2f)\n",
               my_rank, precacheVids.size(), remoteVertices.size(), precache_ratio_);

        getRemoteData(precacheVids);

        unordered_set<unsigned> pendingSet(precacheVids.begin(), precacheVids.end());
        {
            std::unique_lock<std::mutex> lk(g_dataReadyMtx);
            g_dataReadyCv.wait(lk, [&pendingSet] {
                for (unsigned vid : pendingSet) {
                    if (bitmap->get(my_rank, vid) != LOCAL) return false;
                }
                return true;
            });
        }

        printf("[rank %d] precache: %zu vertices ready\n", my_rank, precacheVids.size());
    }

    vector<unsigned> analyzeBatchRemoteVids(const vector<Task>& tasks) {
        ScopedTimer prof(Profiler::T_ANALYZE_REMOTE);
        unordered_set<unsigned> remoteSet;
        for (const Task& tk : tasks) {
            P_ID current_match_PID = p->getcurrent_match_PID(tk.centerIdx);
            const unsigned* tmp = tk.snapshot->rows()[current_match_PID].get();
            unsigned length = tk.snapshot->rows()[current_match_PID].length;
            for (unsigned i = 0; i < length; ++i) {
                unsigned vid = tmp[i];
                if (bitmap->get(my_rank, vid) != LOCAL) {
                    remoteSet.insert(vid);
                }
            }
        }
        return vector<unsigned>(remoteSet.begin(), remoteSet.end());
    }

    void prefetchLoop() {
        prefetchThreadSlot_ = Profiler::instance().registerThread("prefetch");
        ScopedActive profPrefetch(prefetchThreadSlot_);
        int produceIdx = 0;
        while (true) {
            {
                std::unique_lock<std::mutex> lk(slotMtx_);
                slotCv_.wait(lk, [this, produceIdx] {
                    return slotConsumed_[produceIdx].load(std::memory_order_acquire);
                });
            }

            PrefetchBatch& buf = prefetchBufs_[produceIdx];
            buf.clear();
            bool found = collectBatchFromQueues(buf);
            if (!found) {
                if (inFlightTasks_.load(std::memory_order_acquire) == 0) {
                    std::atomic_thread_fence(std::memory_order_acquire);
                    if (levelQueues->empty()) {
                        prefetchDone_.store(true, std::memory_order_release);
                        slotReady_[produceIdx].store(true, std::memory_order_release);
                        {
                            std::lock_guard<std::mutex> lk(slotMtx_);
                            slotCv_.notify_all();
                        }
                        break;
                    }
                }
                {
                    std::unique_lock<std::mutex> lk(slotMtx_);
                    slotCv_.wait_for(lk, std::chrono::microseconds(200));
                }
                continue;
            }

            vector<unsigned> remoteVids = analyzeBatchRemoteVids(buf.tasks());
            if (!remoteVids.empty()) {
                buf.pendingVids().insert(remoteVids.begin(), remoteVids.end());
                {
                    ScopedTimer profGR(Profiler::T_GET_REMOTE);
                    getRemoteData(remoteVids);
                }
            } else {
                buf.dataReady() = true;
            }

            slotConsumed_[produceIdx].store(false, std::memory_order_release);
            slotReady_[produceIdx].store(true, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(slotMtx_);
                slotCv_.notify_all();
            }

            produceIdx = (produceIdx + 1) % PIPELINE_DEPTH;
        }
    }

    void searchALLPR(Graph *g, Pattern *p)
    {
        mainThreadSlot_ = Profiler::instance().registerThread("main/searchALLPR");
        computeThreadSlot_ = Profiler::instance().registerThread("compute/searchPG");
        ScopedTimer profAll(Profiler::T_SEARCH_ALL);
        ScopedActive profMain(mainThreadSlot_);

        int maxLevel = p->getcenter_order_size();
        levelQueues = new LevelTaskQueues(maxLevel);

        int minMatchID = p->getminMatchID();
        int degree_P_in = degree_P[minMatchID].indeg;
        int degree_P_out = degree_P[minMatchID].outdeg;
        
        for (unsigned j = 0; j < g->getnum_v(); j++)
        {
            if (degree_R[j].nodeid != my_rank)  continue;     
            if (degree_R[j].deg >= degree_P_in +degree_P_out 
            && islabelEqual(g->getvlabel(j), p->getvlabel(minMatchID))
            )
            {
                minMatchID_PMR.emplace_back(j);
            }
        }
        unsigned minMatchID_PMR_num = minMatchID_PMR.size();

        printf("pro %d minMatchID_PMR_num = %u \n", my_rank, minMatchID_PMR_num);

        for(size_t i = 0; i < minMatchID_PMR_num; i=i+512){
            if(i+512>=minMatchID_PMR_num){
                Multithreaded_search(i, minMatchID_PMR_num);
            } else {
                Multithreaded_search(i, i+512);
            }
        }
        cout<<"initial tasks pushed"<<endl;
        StartTimer(CONCURR_TIMER);

        prefetchDone_.store(false, std::memory_order_release);
        for (int i = 0; i < PIPELINE_DEPTH; ++i) {
            slotReady_[i].store(false, std::memory_order_release);
            slotConsumed_[i].store(true, std::memory_order_release);
        }

        std::thread prefetchThread(&PMiner::prefetchLoop, this);

        int consumeIdx = 0;
        while (true) {
            {
                std::unique_lock<std::mutex> lk(slotMtx_);
                slotCv_.wait(lk, [this, consumeIdx] {
                    return slotReady_[consumeIdx].load(std::memory_order_acquire)
                        || prefetchDone_.load(std::memory_order_acquire);
                });
            }

            if (prefetchDone_.load(std::memory_order_acquire)
                && !slotReady_[consumeIdx].load(std::memory_order_acquire)) {
                for (int i = 0; i < PIPELINE_DEPTH; ++i) {
                    if (slotReady_[i].load(std::memory_order_acquire)) {
                        PrefetchBatch& doneBuf = prefetchBufs_[i];
                        if (!doneBuf.tasks().empty()) {
                            {
                                ScopedTimer profWR(Profiler::T_WAIT_READY);
                                ScopedWait profWait(mainThreadSlot_);
                                doneBuf.waitForReady();
                            }
                            dispatchBatchToPool(doneBuf);
                        }
                        slotReady_[i].store(false, std::memory_order_release);
                        slotConsumed_[i].store(true, std::memory_order_release);
                    }
                }
                break;
            }

            PrefetchBatch& buf = prefetchBufs_[consumeIdx];

            {
                ScopedTimer profWR(Profiler::T_WAIT_READY);
                ScopedWait profWait(mainThreadSlot_);
                buf.waitForReady();
            }
            dispatchBatchToPool(buf);

            slotReady_[consumeIdx].store(false, std::memory_order_release);
            slotConsumed_[consumeIdx].store(true, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(slotMtx_);
                slotCv_.notify_all();
            }

            consumeIdx = (consumeIdx + 1) % PIPELINE_DEPTH;
        }

        prefetchThread.join();
        tg.wait();

        for(int i=0;i<WORKER_NUM;i++){
            cout<<"pro "<<my_rank<<" to pro "<<i<<" request num: "<<request_num_to_other[i]<<endl;
            cout<<"pro "<<my_rank<<" from pro "<<i<<" response num: "<<response_num_from_other[i]<<endl;
        }
        cout<<"pro "<<my_rank<<" total request num: "<<request_num<<endl;
        cout<<"total_time: "<<g_total_us.load()<<endl;
        StopTimer(CONCURR_TIMER);
        cout << "pro " << my_rank << " concurr time elapse:" <<get_timer(CONCURR_TIMER) << " s" << endl;

        StartTimer(WAIT_TIMER);
        tg.wait();
        StopTimer(WAIT_TIMER);
        cout <<"pro "<<my_rank<< " mining result count is: " << finalAns << endl;

        delete levelQueues;
    }

    bool collectBatchFromQueues(PrefetchBatch& batch) {
        ScopedTimer prof(Profiler::T_COLLECT_BATCH);
        batch.clear();
        unsigned collected = 0;
        for (int level = levelQueues->maxLevel - 1; level >= 0; --level) {
            while (collected < PREFETCH_BATCH_SIZE && levelQueues->size(level) > 0) {
                Task tk;
                if (levelQueues->try_pop(level, tk)) {
                    batch.tasks().push_back(tk);
                    collected++;
                } else {
                    break;
                }
            }
            if (collected >= PREFETCH_BATCH_SIZE) break;
        }
        return collected > 0;
    }

    void dispatchBatchToPool(PrefetchBatch& batch) {
        ScopedTimer prof(Profiler::T_DISPATCH);
        Profiler::instance().incCounter(Profiler::C_BATCHES);
        int count = batch.tasks().size();
        inFlightTasks_.fetch_add(count, std::memory_order_release);
        for (const Task& tk : batch.tasks()) {
            Task tkCopy = tk;
            tg.run([this, tkCopy] {
                finalAns.fetch_add(searchPG(tkCopy));
                inFlightTasks_.fetch_sub(1, std::memory_order_release);
            });
        }
    }

    // 常规图多线程核心函数
    void Multithreaded_search(unsigned i, unsigned j)
    {
        CowSnapshot* snapshot = CowSnapshot::create(p->getnum_v());
        auto data = make_array_shared(j-i);
        std::memcpy(data.get(), minMatchID_PMR.data()+i, (j-i)*sizeof(unsigned int));
        snapshot->rows()[p->getminMatchID()] = RowSlice(data, j-i);
        TravSet tset;
        Task task(snapshot, tset, 0);
        levelQueues->push(0, task);
        totalTaskCount.fetch_add(1, std::memory_order_relaxed);
        Profiler::instance().incCounter(Profiler::C_TOTAL_TASKS);
    }


    unsigned long long searchPG(Task tk)
    {
        ScopedTimer prof(Profiler::T_SEARCH_PG);
        ScopedActive profAct(computeThreadSlot_);
        P_ID current_match_PID = p->getcurrent_match_PID(tk.centerIdx);
        unsigned int* tmp = tk.snapshot->rows()[current_match_PID].get();
        unsigned length = tk.snapshot->rows()[current_match_PID].length;
        bool partialFlag = false;
        unsigned prePidM;
        if(p->partial_order[current_match_PID] != -1){
            partialFlag = true;
            int prePid = p->partial_order[current_match_PID];
            prePidM = tk.snapshot->rows()[prePid].get()[0];
        }

        if(tk.centerIdx+1 < p->getcenter_order_size()){
            for(int i=0;i<length;i++){
                R_ID tmpid = tmp[i];
                if(partialFlag && tmpid <= prePidM) continue;
                if(tk.trav.test(tmpid) == 0){
                    tk.trav.push(tmpid);
                    unsigned int num_v = p->getnum_v();
                    CowSnapshot* output = CowSnapshot::create(num_v);
                    output->numRow = num_v;
                    if(!extendEdgePattern_new(output, tk.centerIdx, tmpid, tk.snapshot, tk.trav)){
                        output->release();
                        tk.snapshot->release();
                        return (unsigned long long)0;
                    }
                    unsigned int after_index = tk.centerIdx+1;
                    Task new_task(output, tk.trav, after_index);
                    levelQueues->push(after_index, new_task);
                    totalTaskCount.fetch_add(1, std::memory_order_relaxed);
                    Profiler::instance().incCounter(Profiler::C_TOTAL_TASKS);
                    tk.trav.len--;
                }
            }
            tk.snapshot->release();
            return (unsigned long long)0;
        }else{
            unsigned long long tmp_count = 0;
            for(int i=0;i<length;i++){
                R_ID tmpid = tmp[i];
                if(partialFlag && tmpid <= prePidM) continue;
                if(tk.trav.test(tmpid) == 0){
                    tk.trav.push(tmpid);
                    unsigned int num_v = p->getnum_v();
                    std::vector<RowSlice> new_rows(num_v);
                    if(!extendEdgePattern_final_new(new_rows, tk.centerIdx, tmpid, tk.snapshot, tk.trav)){
                        tk.snapshot->release();
                        return (unsigned long long)0;
                    }
                    tmp_count += count_set_from_rows(new_rows.data(), num_v, tk.trav);
                    tk.trav.len--;
                }
            }
            tk.snapshot->release();
            return tmp_count;
        }
    }

    bool extendEdgePattern_new(CowSnapshot* output, unsigned int index, R_ID current_match_RID, CowSnapshot* input_snapshot, TravSet& isTraversed){
        ScopedTimer prof(Profiler::T_EXTEND);
        const uint32_t num_v = p->getnum_v();
        std::vector<RowSlice> new_rows(num_v);
    
        // === Step 1: 使用预计算的 needs_update_per_level 判断是否可共享 ===
        const std::vector<bool>& need_update = p->needs_update_per_level[index];  // 引用预计算结果
    
        for (uint32_t v = 0; v < num_v; ++v) {
            if(v == p->getcurrent_match_PID(index)){
                auto data_ptr = make_single_shared(current_match_RID);
                new_rows[v] = RowSlice(data_ptr, 1);
            } else {
                if (!need_update[v]) {
                    // ✅ 完全未被修改 → 直接共享原 slice
                    new_rows[v] = input_snapshot->rows()[v];
                } else {
                    // 🟡 会被修改 → 留空，等待后续填充（写时复制）
                    new_rows[v] = RowSlice{};  // 默认构造，不指向任何有效数据
                }
            }
        }

        // === Step 2: 执行原始挖掘逻辑（min_schedule / int_schedule / equivalent group）===
    
        const unsigned* nbr_data = nullptr;
        unsigned nbr_len = 0;
        bool found = g->getNeighbors(current_match_RID, nbr_data, nbr_len);
        assert(found && "Neighbor data not available");
    
        int min_size = p->min_schedule[index].size();
        int int_size = p->int_schedule[index].size();
        int group_size = p->equivalent_group_schedule_final[index].size();
    
        // --- 处理 min_schedule：扩展边 ---
        if (min_size > 0) {
            for(int i=0;i<min_size;i++){
                P_ID tmp_p=p->min_schedule[index][i];
                vector<unsigned int> tmp;
                tmp.reserve(nbr_len);
                for(unsigned j=0; j < nbr_len ; ++j){
                    R_ID tmpid = nbr_data[j];
                    if(isTraversed.test(tmpid) == 0){
                        int degree_R_deg = g->getR_deg(tmpid);
                        if(degree_R_deg >= degree_P[tmp_p].indeg + degree_P[tmp_p].outdeg){
                            tmp.push_back(tmpid);
                        }
                    }
                }
                if(tmp.size()==0){
                    return false;
                }
                auto data_ptr = make_array_shared(tmp.size());
                std::memcpy(data_ptr.get(), tmp.data(), tmp.size() * sizeof(unsigned int));
                new_rows[tmp_p] = RowSlice(data_ptr, tmp.size());

            }
        }
    
        // --- 处理 int_schedule：求交 ---
        if (int_size > 0) {
            for (int j = 0; j < int_size; ++j) {
                P_ID tmp_p = p->int_schedule[index][j];
                const RowSlice& cur_slice = input_snapshot->rows()[tmp_p];
                const unsigned int* cur_data = cur_slice.get();
                size_t cur_len = cur_slice.length;
    
                std::vector<R_ID> intersect_result;
                intersect_result.reserve(std::min((size_t)nbr_len, cur_len));
                unsigned istart = 0;
                size_t jstart = 0;
    
                while (istart < nbr_len && jstart < cur_len) {
                    R_ID r_nbr = nbr_data[istart];
                    R_ID c_val = cur_data[jstart];
    
                    if (r_nbr == c_val) {
                        if (isTraversed.test(r_nbr) == 0) {
                            intersect_result.push_back(r_nbr);
                        }
                        ++istart;
                        ++jstart;
                    } else if (r_nbr < c_val) {
                        ++istart;
                    } else {
                        ++jstart;
                    }
                }
    
                if (intersect_result.empty()) return false;
    
                auto data_ptr = make_array_shared(intersect_result.size());
                std::memcpy(data_ptr.get(), intersect_result.data(), intersect_result.size() * sizeof(unsigned int));
                new_rows[tmp_p] = RowSlice(data_ptr, intersect_result.size());
            }
        }
    
        // --- 处理 equivalent_group_schedule_final ---
        if (group_size > 0) {
            for (int j = 0; j < group_size; ++j) {
                const auto& group = p->equivalent_group_schedule_final[index][j];
                if (group.size() <= 1) continue;
                P_ID src_v = group[0];
                const RowSlice& src_slice = new_rows[src_v];
                for (size_t k = 1; k < group.size(); ++k) {
                    new_rows[group[k]] = src_slice;
                }
            }
        }

        // === Step 3: 创建输出快照 ===
        std::copy(new_rows.begin(), new_rows.end(), output->rows());
        return true;
      }

      bool extendEdgePattern_final_new(std::vector<RowSlice>& new_rows, unsigned int index, R_ID current_match_RID, CowSnapshot* input_snapshot, TravSet& isTraversed){
        ScopedTimer prof(Profiler::T_EXTEND_FINAL);
        const uint32_t num_v = p->getnum_v();
        // === Step 1: 使用预计算的 needs_update_per_level 判断是否可共享 ===
        const std::vector<bool>& need_update = p->needs_update_per_level[index];  // 引用预计算结果
    
        for (uint32_t v = 0; v < num_v; ++v) {
            if(v == p->getcurrent_match_PID(index)){
                auto data_ptr = make_single_shared(current_match_RID);
                new_rows[v] = RowSlice(data_ptr, 1);
            } else {
                if (!need_update[v]) {
                    // ✅ 完全未被修改 → 直接共享原 slice
                    new_rows[v] = input_snapshot->rows()[v];
                } else {
                    // 🟡 会被修改 → 留空，等待后续填充（写时复制）
                    new_rows[v] = RowSlice{};  // 默认构造，不指向任何有效数据
                }
            }
        }

        const unsigned* nbr_data = nullptr;
        unsigned nbr_len = 0;
        bool found = g->getNeighbors(current_match_RID, nbr_data, nbr_len);
        assert(found && "Neighbor data not available");
    
        int min_size = p->min_schedule[index].size();
        int int_size = p->int_schedule[index].size();
        int group_size = p->equivalent_group_schedule_final[index].size();
    
        // --- 处理 min_schedule：扩展边 ---
        if (min_size > 0) {
            for(int i=0;i<min_size;i++){
                P_ID tmp_p=p->min_schedule[index][i];
                vector<unsigned int> tmp;
                for(unsigned j=0; j < nbr_len ; ++j){
                    R_ID tmpid = nbr_data[j];
                    if(isTraversed.test(tmpid) == 0){
                        int degree_R_deg = g->getR_deg(tmpid);
                        if(degree_R_deg >= degree_P[tmp_p].indeg + degree_P[tmp_p].outdeg){
                            tmp.push_back(tmpid);
                        }
                    }
                }
                if(tmp.size()==0){
                    return false;
                }
                auto data_ptr = make_array_shared(tmp.size());
                std::memcpy(data_ptr.get(), tmp.data(), tmp.size() * sizeof(unsigned int));
                new_rows[tmp_p] = RowSlice(data_ptr, tmp.size());

            }
        }
    
        // --- 处理 int_schedule：求交 ---
        if (int_size > 0) {
            for (int j = 0; j < int_size; ++j) {
                P_ID tmp_p = p->int_schedule[index][j];
                const RowSlice& cur_slice = input_snapshot->rows()[tmp_p];
                const unsigned int* cur_data = cur_slice.get();
                size_t cur_len = cur_slice.length;
    
                std::vector<R_ID> intersect_result;
                unsigned istart = 0;
                size_t jstart = 0;
    
                while (istart < nbr_len && jstart < cur_len) {
                    R_ID r_nbr = nbr_data[istart];
                    R_ID c_val = cur_data[jstart];
    
                    if (r_nbr == c_val) {
                        if (isTraversed.test(r_nbr) == 0) {
                            intersect_result.push_back(r_nbr);
                        }
                        ++istart;
                        ++jstart;
                    } else if (r_nbr < c_val) {
                        ++istart;
                    } else {
                        ++jstart;
                    }
                }
    
                if (intersect_result.empty()) return false;
    
                auto data_ptr = make_array_shared(intersect_result.size());
                std::memcpy(data_ptr.get(), intersect_result.data(), intersect_result.size() * sizeof(unsigned int));
                new_rows[tmp_p] = RowSlice(data_ptr, intersect_result.size());
            }
        }
    
        // --- 处理 equivalent_group_schedule_final ---
        if (group_size > 0) {
            for (int j = 0; j < group_size; ++j) {
                const auto& group = p->equivalent_group_schedule_final[index][j];
                if (group.empty()) continue;
                // Compute full expansion once per group, share across all members
                vector<unsigned int> tmp;
                tmp.reserve(nbr_len);
                for(unsigned j2=0; j2 < nbr_len; ++j2){
                    R_ID tmpid = nbr_data[j2];
                    if(isTraversed.test(tmpid) == 0){
                        tmp.push_back(tmpid);
                    }
                }
                if(tmp.size()==0) return false;
                auto data_ptr = make_array_shared(tmp.size());
                std::memcpy(data_ptr.get(), tmp.data(), tmp.size() * sizeof(unsigned int));
                RowSlice shared(data_ptr, tmp.size());
                for (P_ID tmp_p : group) {
                    new_rows[tmp_p] = shared;
                }
            }
        }

        return true;
      }


    void status_sync(int isidle){
        if(my_rank != MASTER_RANK){
            MPI_Send(&isidle, 1, MPI_INT, MASTER_RANK, STATUS_SYNC_CHANNEL, MPI_COMM_WORLD);
            //cout<<"processor "<<my_rank<<" 11111 isAllIdle: "<<isAllIdle<<endl;
            int all_idle;
            MPI_Recv(&all_idle, 1, MPI_INT, MASTER_RANK, STATUS_SYNC_CHANNEL, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            if(all_idle)    isAllIdle = true;
            //cout<<"processor "<<my_rank<<" 22222 isAllIdle: "<<isAllIdle<<endl;
        }else{
            int all_idle = isidle;
            for(int i = 0; i < WORKER_NUM; i++){
                if(i != MASTER_RANK){
                    int idle;
                    MPI_Recv(&idle, 1, MPI_INT, i, STATUS_SYNC_CHANNEL, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    //cout<<"processor "<<my_rank<<" 33333 isAllIdle: "<<isAllIdle<<endl;
                    all_idle = all_idle && idle;
                }
            }
            for(int i = 0; i < WORKER_NUM; i++){
                if(i != MASTER_RANK){
                    MPI_Send(&all_idle, 1, MPI_INT, i, STATUS_SYNC_CHANNEL, MPI_COMM_WORLD);
                }
            }
            if(all_idle)    isAllIdle = true;
            //cout<<"processor "<<my_rank<<" 44444 isAllIdle: "<<isAllIdle<<endl;
        }
    }


    // 归并移除 
    void merge_un_set(std::vector<unsigned>& v1, std::vector<unsigned>& v2, std::unordered_set<unsigned>& un_set){
        int i,j;
        // 求交
        i = j = 0;//定位到2个有序向量的头部
        while(i < v1.size() && j < v2.size())
        {
            if(v1[i] == v2[j]) //相等则为交集的元素  有交集则需要求并
            {
                un_set.insert(v1[i]);
                i += 1;
                j += 1;
            }
            else if(v1[i] < v2[j])   //不相等时，指示较小元素额标记加一
            {
                i += 1;
            }
            else
            {
                j += 1;
            }
        }
    }

    void merge_set(std::vector<unsigned>& v1, std::vector<unsigned>& v2, std::vector<unsigned>& v3){
        int i,j;
        // 求交
        i = j = 0;//定位到2个有序向量的头部
        while(i < v1.size() && j < v2.size())
        {
            if(v1[i] == v2[j]) //相等则为交集的元素  有交集则需要求并
            {
                v3.push_back(v1[i]);
                i += 1;
                j += 1;
            }
            else if(v1[i] < v2[j])   //不相等时，指示较小元素额标记加一
            {
                i += 1;
            }
            else
            {
                j += 1;
            }
        }
    }

    unsigned merge_count(std::vector<unsigned>& v1, std::vector<unsigned>& v2){
        unsigned cur_count = 0;
        int i,j;
        // 求交
        i = j = 0;//定位到2个有序向量的头部
        while(i < v1.size() && j < v2.size())
        {
            if(v1[i] == v2[j]) //相等则为交集的元素  有交集则需要求并
            {
                i += 1;
                j += 1;
                cur_count += 1; // 记录求交个数
            }
            else if(v1[i] < v2[j])   //不相等时，指示较小元素额标记加一
            {
                i += 1;
            }
            else
            {
                j += 1;
            }
        }

        return cur_count;
    }

    bool eliminate(std::vector<unsigned> &PMR_remain, const std::vector<unsigned> &isTraversed){
        if(isTraversed.empty()) return false;
        bool flag = false;
        size_t write = 0, i = 0, j = 0;
        while (i < PMR_remain.size() && j < isTraversed.size()) {
            if (PMR_remain[i] < isTraversed[j]) {
                PMR_remain[write++] = PMR_remain[i++];
            } else if (PMR_remain[i] > isTraversed[j]) {
                ++j;
            } else {
                flag = true;
                ++i; ++j;
            }
        }
        while (i < PMR_remain.size()) PMR_remain[write++] = PMR_remain[i++];
        PMR_remain.resize(write);
        return flag;
    }

    void eliminate_single(std::vector<unsigned> &row, unsigned val) {
        size_t write = 0;
        for (size_t i = 0; i < row.size(); ++i) {
            if (row[i] != val) row[write++] = row[i];
        }
        row.resize(write);
    }

    void insert_sorted(std::vector<unsigned> &row, unsigned val) {
        auto it = std::lower_bound(row.begin(), row.end(), val);
        row.insert(it, val);
    }

    unsigned set_operation2(std::vector<std::vector<unsigned>> &PMR_copy){
        unsigned cur_count = 0;
        cur_count = PMR_copy[0].size() * PMR_copy[1].size() - merge_count(PMR_copy[0], PMR_copy[1]);
        return cur_count;
    }

    unsigned set_operation3(std::vector<std::vector<unsigned>> &PMR_copy){
        unsigned cur_count = 0;
        std::vector<unsigned> merge;
        int line1 = PMR_copy[0].size();
        int line2 = PMR_copy[1].size();
        int line3 = PMR_copy[2].size();
        merge_set(PMR_copy[0], PMR_copy[1], merge); // 求交结果要用，存储在merge中
        unsigned count12 = merge.size()*line3; // 1，2行重复个数
        unsigned count13 = merge_count(PMR_copy[0], PMR_copy[2])*line2; // 1，3行重复个数
        unsigned count23 = merge_count(PMR_copy[1], PMR_copy[2])*line1; // 2，3行重复个数
        unsigned count123 = merge_count(merge, PMR_copy[2])*2; // 1,2,3行重复个数
        cur_count = line1*line2*line3 - count12 - count13 - count23 + count123;
        return cur_count;
    }

    unsigned set_operation4(std::vector<std::vector<unsigned>> &PMR_copy){
        unsigned cur_count = 0;
        int line1 = PMR_copy[0].size();
        int line2 = PMR_copy[1].size();
        int line3 = PMR_copy[2].size();
        int line4 = PMR_copy[3].size();
        std::vector<unsigned> merge12;
        std::vector<unsigned> merge34;
        std::vector<unsigned> merge123;

        merge_set(PMR_copy[0], PMR_copy[1], merge12); // 12求交结果要用，与3，4求交得到3行求交结果，存储在merge12中
        merge_set(PMR_copy[2], PMR_copy[3], merge34); // 34求交结果要用，与1，2求交得到3行求交结果，存储在merge34中
        merge_set(merge12, PMR_copy[2], merge123); // 123求交结果要用，与4求交得到4行求交结果，存储在merge123中

        unsigned size12 = merge12.size(); // 1，2行重复个数
        unsigned size13 = merge_count(PMR_copy[0], PMR_copy[2]); // 1，3行重复个数
        unsigned size14 = merge_count(PMR_copy[0], PMR_copy[3]); // 1，4行重复个数
        unsigned size23 = merge_count(PMR_copy[1], PMR_copy[2]); // 2，3行重复个数
        unsigned size24 = merge_count(PMR_copy[1], PMR_copy[3]); // 2，4行重复个数
        unsigned size34 = merge34.size(); // 3,4行重复个数
        unsigned size123 = merge123.size(); // 1,2,3行重复个数
        unsigned size124 = merge_count(merge12, PMR_copy[3]); // 1,2,4行重复个数
        unsigned size234 = merge_count(merge34, PMR_copy[1]); // 2,3,4行重复个数
        unsigned size134 = merge_count(merge34, PMR_copy[0]); // 1,3,4行重复个数
        unsigned size1234 = merge_count(merge123, PMR_copy[3]); // 1,2,3,4行重复个数

        unsigned count12 = size12*line3*line4 - size12*size34; // 1，2行重复个数
        unsigned count13 = size13*line2*line4 - size13*size24; // 1，3行重复个数
        unsigned count14 = size14*line2*line3 - size14*size23; // 1，4行重复个数
        unsigned count23 = size23*line1*line4; // 2，3行重复个数
        unsigned count24 = size24*line1*line3; // 2，4行重复个数
        unsigned count34 = size34*line1*line2; // 3, 4行重复个数
        unsigned count123 = size123*2*line4; // 1,2,3行重复个数
        unsigned count124 = size124*2*line3; // 1,2,4行重复个数
        unsigned count234 = size234*2*line1; // 2,3,4行重复个数
        unsigned count134 = size134*2*line2; // 1,3,4行重复个数
        unsigned count1234 = size1234*2 - size1234*2*4; // 1,2,3,4行重复个数


        cur_count = line1*line2*line3*line4 - count12 - count13 - count14 - count23 - count24 - count34 + count123 + count124 + count234 + count134 + count1234 ;
        return cur_count;
    }

    unsigned long long full_permutation2(std::vector<std::vector<unsigned>> &PMR_remain){
        // cout<<3<<endl;
        unsigned long long cur_count = 0;
        int line1 = PMR_remain[0].size();
        int line2 = PMR_remain[1].size();
        for(int i1 = 0; i1 < line1; ++i1){
            int id1 = PMR_remain[0][i1];
            for(int i2 = 0; i2 < line2; ++i2){
            if(id1 != PMR_remain[1][i2]) ++cur_count;
            }
        }
        return cur_count;
    }

    unsigned long long full_permutation3(std::vector<std::vector<unsigned>> &PMR_remain){
        // cout<<3<<endl;
        unsigned long long cur_count = 0;
        int line1 = PMR_remain[0].size();
        int line2 = PMR_remain[1].size();
        int line3 = PMR_remain[2].size();
        for(int i1 = 0; i1 < line1; ++i1){
            int id1 = PMR_remain[0][i1];
            for(int i2 = 0; i2 < line2; ++i2){
            int id2 = PMR_remain[1][i2];
            if(id1 != id2){
                for(int i3 = 0; i3 < line3; ++i3){
                int id3 = PMR_remain[2][i3];
                if(id3!=id1 && id3!=id2)  ++cur_count;
                }
            }
            }
        }
        return cur_count;
    }

    unsigned long long full_permutation4(std::vector<std::vector<unsigned>> &PMR_remain){
        // cout<<4<<endl;
        unsigned long long cur_count = 0;
        int line1 = PMR_remain[0].size();
        int line2 = PMR_remain[1].size();
        int line3 = PMR_remain[2].size();
        int line4 = PMR_remain[3].size();
        for(int i1 = 0; i1 < line1; ++i1){
            int id1 = PMR_remain[0][i1];
            for(int i2 = 0; i2 < line2; ++i2){
            int id2 = PMR_remain[1][i2];
            if(id1 != id2){
                for(int i3 = 0; i3 < line3; ++i3){
                int id3 = PMR_remain[2][i3];
                if(id3!=id1 && id3!=id2){
                    for(int i4 = 0; i4 < line4; ++i4){
                    int id4 = PMR_remain[3][i4];
                    if(id4!=id3 && id4!= id2 && id4!= id1)   ++cur_count;
                    }
                }
                }
            }
            }
        }
        return cur_count;
    }

    unsigned long long full_permutation5(std::vector<std::vector<unsigned>> &PMR_remain){
        // cout<<5<<endl;
        unsigned long long cur_count = 0;
        int line1 = PMR_remain[0].size();
        int line2 = PMR_remain[1].size();
        int line3 = PMR_remain[2].size();
        int line4 = PMR_remain[3].size();
        int line5 = PMR_remain[4].size();

        for(int i1 = 0; i1 < line1; ++i1){
            int id1 = PMR_remain[0][i1];
            for(int i2 = 0; i2 < line2; ++i2){
            int id2 = PMR_remain[1][i2];
            if(id1 != id2){
                for(int i3 = 0; i3 < line3; ++i3){
                int id3 = PMR_remain[2][i3];
                if(id3!=id1 && id3!=id2){
                    for(int i4 = 0; i4 < line4; ++i4){
                    int id4 = PMR_remain[3][i4];
                    if(id4!=id1 && id4!=id3 && id4!= id2 && id4!= id1){
                        for(int i5 = 0; i5 < line5; ++i5){
                        int id5 = PMR_remain[4][i5];
                        if(id5!=id4 && id5!=id3 && id5!= id2 && id5!= id1) ++cur_count;
                        }
                    }
                    }
                }
                }
            }
            }
        }
        return cur_count;
    }

    unsigned long long full_permutation(std::vector<std::vector<unsigned>> &PMR_remain, int index, unordered_set<unsigned>& set){
        if(index == PMR_remain.size()) return 1;
        unsigned long long cur_count = 0;
        int line = PMR_remain[index].size();
        for(int i = 0; i < line; ++i){
            int id = PMR_remain[index][i];
            if(set.count(id) == 0){
            set.insert(id);
            cur_count += full_permutation(PMR_remain, index+1, set);
            set.erase(id);
            }
        }
        return cur_count;
    }

    unsigned long long count_set_from_rows(const RowSlice* rows, unsigned int numRow, const TravSet& trav) {
        int need_full = p->getneed_full();
        if(need_full == 0) return 1;

        vector<vector<unsigned>> PMR_remain(need_full);
        for(int i = 0; i < need_full; ++i){
            int ful = p->getfull(i);
            const unsigned* data = rows[ful].get();
            size_t len = rows[ful].length;
            PMR_remain[i].assign(data, data + len);
        }

        vector<unsigned> isTraversed_v(trav.buf, trav.buf + trav.len);
        sort(isTraversed_v.begin(), isTraversed_v.end());
        for(int i = 0; i < need_full; ++i){
            eliminate(PMR_remain[i], isTraversed_v);
        }

        if(need_full == 1) return PMR_remain[0].size();
        else if(need_full == 2) return set_operation2(PMR_remain);
        else if(need_full == 3) return set_operation3(PMR_remain);
        else if(need_full == 4) return set_operation4(PMR_remain);
        else if(need_full == 5) return count_set5(PMR_remain);
        else return count_set_generic(PMR_remain);
    }

    unsigned long long count_set5(vector<vector<unsigned>>& PMR_remain) {
        unsigned long long result = 0;
        int min_size = PMR_remain[0].size();
        int min_index = 0;
        for(int i = 1; i < 5; ++i){
            int sz = PMR_remain[i].size();
            if(sz < min_size){ min_size = sz; min_index = i; }
        }

        unordered_set<unsigned> union_set;
        vector<unsigned> full_v(std::move(PMR_remain[min_index]));
        PMR_remain.erase(PMR_remain.begin() + min_index);
        for(int i = 0; i < 4; ++i){
            merge_un_set(full_v, PMR_remain[i], union_set);
        }

        unsigned four_line_count = 0;
        if(union_set.size() < full_v.size()){
            four_line_count = set_operation4(PMR_remain);
        }

        int full_v_size = full_v.size();
        for(int i = 0; i < full_v_size; ++i){
            unsigned rid = full_v[i];
            if(union_set.count(rid) != 0){
                for(int j = 0; j < 4; ++j){
                    eliminate_single(PMR_remain[j], rid);
                }
                result += set_operation4(PMR_remain);
                for(int j = 0; j < 4; ++j){
                    insert_sorted(PMR_remain[j], rid);
                }
            }else{
                result += four_line_count;
            }
        }
        return result;
    }

    unsigned long long count_set_generic(vector<vector<unsigned>>& PMR_remain) {
        int need_full = PMR_remain.size();
        unordered_set<unsigned> set;
        return full_permutation(PMR_remain, 0, set);
    }

    unsigned long long count_set(std::vector<std::vector<unsigned>> &PMR_copy, const unordered_set<R_ID>& isTraversed){
        int need_full = p->getneed_full();
        if(need_full == 0){
            return 1;
        }else{
            // 1 将PMR_copy中非中心点的匹配集合复制给PMR_remain
            vector<vector<unsigned>> PMR_remain;  // 存储非中心点的匹配集合
            PMR_remain.resize(need_full);

            for(int i = 0 ; i< need_full; ++i){
            PMR_remain[i].swap(PMR_copy[p->getfull(i)]);
            }

            // 2 剔除PMR_remain每行中的作为中心点的点
            // isTraversed_v 需要先排序
            vector<unsigned> isTraversed_v(isTraversed.begin(), isTraversed.end());
            sort(isTraversed_v.begin(), isTraversed_v.end());
            for(int i = 0 ; i<need_full; ++i){
            eliminate(PMR_remain[i], isTraversed_v);
            }


            if(need_full == 1){
            return PMR_remain[0].size();
            }else if(need_full == 2){
            return set_operation2(PMR_remain);
            }else if (need_full == 3){
            return set_operation3(PMR_remain);
            }else if ( need_full == 4 ){
            return set_operation4(PMR_remain);
            }else if ( need_full == 5 ){
            unsigned long long result = 0;
            // 1 找到PMR_remain中元素个数最少的行 作为最外层的循环
            int min_size = PMR_remain[0].size();
            int min_index = 0;
            for(int i = 1; i<need_full; ++i){
                int PMR_remain_size = PMR_remain[i].size();
                if(PMR_remain_size < min_size){
                min_size = PMR_remain_size;
                min_index = i;
                }
            }

            // 2 计算第一行中的元素有哪些在后续的行中出现并记录在union_set
            unordered_set<unsigned> union_set; // 第一行和后面行求交的结果都存储在union_v中
            vector<unsigned> full_v(PMR_remain[min_index]);
            PMR_remain.erase(PMR_remain.begin()+min_index);
            for(int i = 0; i<4;++i){
                merge_un_set(full_v, PMR_remain[i], union_set);
            }
            // cout<<"union_set: ";
            // for(auto it = union_set.begin(); it != union_set.end(); ++it){
            //   cout<<*it<<" ";
            // }
            // cout<<endl;
            // cout<<"gggggg"<<endl;

            // 3 计算第一行和后四行没有交集的情况下，后四行全排列的个数
            unsigned four_line_count = 0;
            if(union_set.size() < full_v.size()){
                four_line_count = set_operation4(PMR_remain);
            }
            // cout<<"hhhhhhh"<<endl;
            
            // 4 全排第一行
            int full_v_size = full_v.size();
            for(int i = 0 ; i<full_v_size;++i){

                unsigned rid = full_v[i];
                // cout<<"rid: "<<rid<<endl;
                if(union_set.count(rid) != 0){
                vector<vector<unsigned>> PMR_remain_copy(PMR_remain);
                vector<unsigned> isTraversed_cur_v;
                isTraversed_cur_v.push_back(rid);
                // cout<<"lllllll"<<endl;
                // print_PMR(PMR_remain_copy);
                for(int j = 0; j<4;++j){
                    eliminate(PMR_remain_copy[j], isTraversed_cur_v);
                }
                // cout<<"nnnnnn"<<endl;

                result += set_operation4(PMR_remain_copy);
                }else{
                result += four_line_count;
                }
            }
            return result;
            }else{
            cout<<"con not use set operation optimization..."<<endl;
            }
        }
    }

    unsigned long long count_full(std::vector<std::vector<unsigned>> &PMR_copy, const unordered_set<R_ID>& isTraversed){
        int need_full = p->getneed_full();
        // cout<<"====================count_full=============="<<endl;
        if(need_full == 0){
            return 1;
        }else{
            // 1 将PMR_copy中非中心点的匹配集合复制给PMR_remain
            vector<vector<unsigned>> PMR_remain;  // 存储非中心点的匹配集合
            PMR_remain.resize(need_full);

            // 1.1 匹配数多的放在外层
            multimap<int, int> mp; // <行匹配数， 行号>
            for(int i = 0; i < need_full; ++i){
                int ful = p->getfull(i); 
                mp.insert({PMR_copy[ful].size(), ful});
            }
            int tindex = 0;
            for(auto& t : mp){
            PMR_remain[tindex].swap(PMR_copy[t.second]);
            ++tindex;
            }

            // 2 剔除PMR_remain每行中的作为中心点的点
            // isTraversed_v 需要先排序
            vector<unsigned> isTraversed_v(isTraversed.begin(), isTraversed.end());
            sort(isTraversed_v.begin(), isTraversed_v.end());
            for(int i = 0 ; i<need_full; ++i){
            eliminate(PMR_remain[i], isTraversed_v);
            }


            if(need_full == 1){
            return PMR_remain[0].size();
            }else if(need_full == 2){
            return full_permutation2(PMR_remain);
            }else if (need_full == 3){
            return full_permutation3(PMR_remain);
            }else if ( need_full == 4 ){
            return full_permutation4(PMR_remain);
            }else if ( need_full == 5 ){
            return full_permutation5(PMR_remain);
            }else{
            unordered_set<unsigned> set;
            return full_permutation(PMR_remain, 0, set);
            }
        }
    }



public:

    PMiner(Graph *graph, Pattern *pattern, int threadNum, double precache_ratio = 0.0) 
    {
        g = graph;
        p = pattern;
        ThreadNum = threadNum;
        precache_ratio_ = max(0.0, min(precache_ratio, 1.0));
        degree_P = p->getdegree_P();
        degree_R = g->getdegree_R();
        finalAns = 0;

        cache = new Cache(MAX_CACHE_SIZE);
    }

    ~PMiner(){
        delete g;
        delete p;
        delete bitmap;
        delete cache;
    }
    //多线程
    void run(){
        tbb::task_scheduler_init init(ThreadNum);
        std::set<unsigned> my_set =g->getLocalVer();
  unsigned size=std::distance(my_set.begin(), my_set.end());
  unsigned *buffer=new unsigned[size*2];
  //vector<Degree_global> local_degree;
  //Degree_global data;
  unsigned i=0;
  for(unsigned id:my_set){
    buffer[i]=id;
    buffer[i+1]=g->getR_deg(id);
    //buffer[i+2]=g->getR_outdeg(id);
    i=i+2;
  }
  //global_degree.assign(local_degree.begin(),local_degree.end());
  
    

    // MPI_Get_address(&(struct degree.id), &displacements[0]);
    // MPI_Get_address(&(data.indeg), &displacements[1]);
    // MPI_Get_address(&(data.outdeg), &displacements[2]);

   
    
    for(int i=0;i<WORKER_NUM;i++){
  if(i!=my_rank){
    MPI_Send(buffer, size*2, MPI_UNSIGNED, i, tb_msg, MPI_COMM_WORLD);
    // if(a==MPI_SUCCESS){
    //     cout<<"chenggong"<<endl;
    // }
    //cout<<"yifasong"<<endl;
  }
 }
 
 //cout<<"wancheng"<<endl;
 while(num<WORKER_NUM-1){
    //cout<<num<<endl;
  usleep(WAIT_TIME_WHEN_IDLE);
 }
 delete[]buffer;
 my_set.clear();
 //degree_R=g->getdegree_R();
 gd.stop();

        precacheHighDegreeVertices();
        worker_barrier();

        start_timer(TOTAL_TIMER);
        searchALLPR(g, p);
        // if (p->getisEqCircle())
        // {
        //     cout<<"Graph is EqCircle"<<endl;
        //     searchALLCircle(g, p); 
        // }
        // else{
        //     cout << "Graph is not EqCircle" << endl;
        //     searchALLPR(g, p);
        // }
        stop_timer(TOTAL_TIMER);
        cout << "pro " << my_rank << " graph mining time elapse:" <<get_timer(TOTAL_TIMER) << " s" << endl;
        status_sync(1);    //注意修改worker_num
        //worker_barrier();
        if(isAllIdle == true)   cout<<"processor: "<<my_rank<<" all finish"<<endl;
        gout.close();
        // PrintTimer("CONCURR_TIMER", CONCURR_TIMER);
        // PrintTimer("COMPUTE_TIMER", COMPUTE_TIMER);
        // PrintTimer("COMMUNICATION_TIMER", COMMUNICATION_TIMER);
        // PrintTimer("SLEEP_TIMER", SLEEP_TIMER);
        // PrintTimer("WAIT_TIMER", WAIT_TIMER);
    }

    //建立位图 type = 0, hash；type = 1, bdg
    bool buildBitMap(int type, string inputfile, unsigned vertexNum,int my_rank){
        // printf("WORKER_NUM: %d, vertexNUM: %d\n",WORKER_NUM, vertexNum);
        bitmap = new ConcurrentBitMap(WORKER_NUM,vertexNum,my_rank);
        switch (type)
        {
        case 0:
            bitmap->initBitMap(0);  //hash
            break;
        case 1:
            bitmap->initBitMap(1, inputfile); //bdg
            break;
        default:
            bitmap->initBitMap(0);  //hash
            break;
        }
        // //判断数据完整性
        // set<R_ID> localVer = g->getLocalVer();
        // for(auto it = localVer.begin(); it != localVer.end(); ++it){
        //     R_ID id = *it;
        //     bool flag = true;
        //     //正向邻居节点
        //     R_ID R_out_start = g->getR_adjIndex(id);
        //     R_ID R_out_end = R_out_start + g->getR_deg(id);
        //     for(R_ID i = R_out_start; i < R_out_end ; i++){
        //         R_ID tmpid = g->getR_adj(i);
        //         if(bitmap->get(my_rank, tmpid) == REMOTE){
        //             flag = false;
        //             break;
        //         }
        //     }
            
        //     //检查并请求所有邻居节点后，该数据肯定是完整的
        //     if(flag) bitmap->set(my_rank,id,INTEGRAL);   
        // }
        return true;
    }
    //打印位图
    void printBitMap(){
        bitmap->printBitMap();
    }
};

#endif // PMINER_H
