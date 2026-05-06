#ifndef REQUEST_H
#define REQUEST_H

#include "../util/global.h"
#include <thread>
#include <vector>
#include <deque>
#include <list>
#include <mutex>
#include <condition_variable>
#include <chrono>




// ---------------------------------------------------------------------------
// 1. 单条应答
struct TaskItem {
    int                   src;
    std::vector<unsigned> data;          // 单条应答
    unsigned nei_num;               // 邻居数量
    size_t bytes() const { return data.size() * sizeof(unsigned); }
};

// 3. 批次缓冲区（三重门限）
class BatchBuf {
	public:
		std::deque<TaskItem> items;
		size_t               totalBytes = 0;
		using clock = std::chrono::steady_clock;
		clock::time_point    birth = clock::now();
	
		bool ready() const {
			return totalBytes >= MAX_BYTES ||
				   items.size() >= MAX_COUNT ||
				   std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - birth).count() >= MAX_US;
		}
		void clear() {
			items.clear();
			totalBytes = 0;
			birth = clock::now();
		}
	private:
		static constexpr size_t MAX_BYTES = 16 * 1024; // 16 kB
		static constexpr size_t MAX_COUNT = 32;        // 32 条
		static constexpr int    MAX_US    = 200;       // 200 µs
};

// ---------------------------------------------------------------------------
// 2. 线程安全批次队列（MPMC）
class BatchQueue {
public:
    void push(class BatchBuf&& b) {
        {
            std::unique_lock<std::mutex> lk(m_);
            q_.emplace_back(std::move(b));
        }
        cv_.notify_one();
    }
    bool pop(class BatchBuf& b) {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [this]{ return !q_.empty() || stop_; });
        if (q_.empty()) return false;
        b = std::move(q_.front());
        q_.pop_front();
        return true;
    }
    void shutdown() {
        { std::unique_lock<std::mutex> lk(m_); stop_ = true; }
        cv_.notify_all();
    }
private:
    std::deque<class BatchBuf> q_;
    std::mutex                 m_;
    std::condition_variable    cv_;
    bool                       stop_ = false;
};

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// 4. Request 类
class Request {
    std::vector<BatchBuf> perRankBatch;
    
    int world_size = 0;
    ThreadSlot* listenSlot_ = nullptr;
    ThreadSlot* workerSlot_ = nullptr;
public:
    Request() {
        world_size = WORKER_NUM;
        perRankBatch.resize(world_size);
        listenSlot_ = Profiler::instance().registerThread("req/listen");
        workerSlot_ = Profiler::instance().registerThread("req/worker");
        int n = std::min(4u, std::thread::hardware_concurrency());
        workers.reserve(n);
        for (int i = 0; i < n; ++i)
            workers.emplace_back(&Request::workerLoop, this);
    }
    ~Request() {
        main_thread.join();
        batchQueue.shutdown();
        for (auto& t : workers) t.join();
        for (auto& ps : pendingSends_) {
            MPI_Wait(&ps.second, MPI_STATUS_IGNORE);
        }
        pendingSends_.clear();
    }

private:
    /* ---------------- listen 线程 ---------------- */
    void listen() {
        try {
        while (!isAllIdle) {
            int flag;  MPI_Status st;
            MPI_Iprobe(MPI_ANY_SOURCE, REQUEST_MSG, MPI_COMM_WORLD, &flag, &st);
            if (flag) {
                ScopedTimer profL(Profiler::T_REQ_LISTEN);
                ScopedActive profAct(listenSlot_);
                Profiler::instance().incCounter(Profiler::C_RECVS);
                int src = st.MPI_SOURCE;
                int cnt;
                MPI_Get_count(&st, MPI_UNSIGNED, &cnt);
                std::vector<unsigned> reqBuf(cnt);
                MPI_Recv(reqBuf.data(), cnt, MPI_UNSIGNED,
                         src, REQUEST_MSG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                TaskItem it{src, {}, 0};
                it.data.insert(it.data.end(), reqBuf.begin(), reqBuf.end());
                auto& bucket = perRankBatch[src];
                for(unsigned i=0;i<cnt;++i)
                {
                    unsigned vid=reqBuf[i];
                    it.nei_num+=g->getdegree(vid);
                }
                bucket.totalBytes += (it.nei_num+cnt)*sizeof(unsigned);
                bucket.items.emplace_back(std::move(it));
                if (bucket.ready()) {
                    batchQueue.push(std::move(bucket));
                    bucket.clear();
                }
            } else {
                for (int r = 0; r < world_size; ++r) {
                    auto& b = perRankBatch[r];
                    if (!b.items.empty() && b.ready()) {
                        batchQueue.push(std::move(b));
                        b.clear();
                    }
                }
                usleep(WAIT_TIME_WHEN_IDLE);
            }
        }
    }
    catch (const std::exception& e) {
        fprintf(stderr,"[rank %d] listen thread exception: %s\n", my_rank, e.what());
        std::abort();
    }
    catch (...) {
        fprintf(stderr,"[rank %d] listen thread unknown exception\n", my_rank);
        std::abort();
    }
        /* ⑥ 收尾：所有桶 */
        for (int r = 0; r < world_size; ++r)
            if (!perRankBatch[r].items.empty())
                batchQueue.push(std::move(perRankBatch[r]));
        //         auto& resp = it.data;
        //         //resp.reserve(128);
        //         //resp.push_back(cnt);                    // 头部：请求条数
        //         resp.insert(resp.end(), reqBuf.begin(), reqBuf.end());
        //         //for (unsigned vid : reqBuf) processSingleVertex(vid, resp);
        //         for(unsigned i=0;i<cnt;++i)
        //         {
        //             unsigned vid=reqBuf[i];
        //             currBatch.totalBytes += sizeof(unsigned)*(g->getdegree(vid)+1); //预估应答大小
        //         }
        //         /* 塞进当前批 */
        //         //currBatch.totalBytes += it.bytes();
        //         currBatch.items.emplace_back(std::move(it));

        //         /* 批满了就交出去 */
        //         if (currBatch.ready()) {
        //             batchQueue.push(std::move(currBatch));
        //             currBatch.clear();
        //         }
        //     } else {
        //         /* 没消息，但可能超时 */
        //         if (!currBatch.items.empty() && currBatch.ready()) {
        //             batchQueue.push(std::move(currBatch));
        //             currBatch.clear();
        //         } else {
        //             usleep(WAIT_TIME_WHEN_IDLE);
        //         }
        //     }
        // }
        // /* 收尾 */
        // if (!currBatch.items.empty()) batchQueue.push(std::move(currBatch));
    }

    /* ---------------- worker 线程 ---------------- */
    void workerLoop() {
        try {
            BatchBuf b;
            while (batchQueue.pop(b)) {
                ScopedTimer profW(Profiler::T_REQ_WORKER);
                ScopedActive profAct(workerSlot_);
                int dst = b.items[0].src;
                std::vector<unsigned> payload;
                size_t total = 1;
                unsigned vertex_count = 0;
                for (auto& it : b.items) {
                    total += it.data.size()+it.nei_num;
                    vertex_count += it.data.size();
                }
                payload.reserve(total);
                payload.push_back(vertex_count);
                for (auto& it : b.items) payload.insert(payload.end(), it.data.begin(), it.data.end());
                for (auto& it : b.items) {
                    for (unsigned i = 0; i < it.data.size(); ++i) {
                        unsigned vid = it.data[i];
                        const unsigned* nbr_data = nullptr;
                        unsigned deg = 0;
                        g->getLocalNeighbors(vid, nbr_data, deg);
                        for(unsigned j = 0; j < deg; ++j) {
                            unsigned nbr = nbr_data[j];
                            payload.push_back(nbr);
                        }
                    }
                }

                MsgHeader hdr;
                hdr.bytes = payload.size() * sizeof(unsigned);
                std::vector<unsigned> msg( (sizeof(hdr)+hdr.bytes-1)/sizeof(unsigned) + 1 );
                unsigned char* p = reinterpret_cast<unsigned char*>(msg.data());
                std::memcpy(p, &hdr, sizeof(hdr));
                std::memcpy(p + sizeof(hdr), payload.data(), hdr.bytes);
                size_t msgWords = (sizeof(hdr) + hdr.bytes + sizeof(unsigned)-1) / sizeof(unsigned);

                {
                    std::lock_guard<std::mutex> lk(sendMtx_);
                    pendingSends_.push_back({std::move(msg), MPI_REQUEST_NULL});
                    MPI_Isend(pendingSends_.back().first.data(), msgWords, MPI_UNSIGNED,
                              dst, RESPONSE_MSG, MPI_COMM_WORLD, &pendingSends_.back().second);
                    Profiler::instance().incCounter(Profiler::C_ISENDS);
                }
                cleanCompletedSends();
            }
        }
        catch (const std::exception& e) {
            fprintf(stderr,"[rank %d] worker thread exception: %s\n", my_rank, e.what());
            std::abort();
        }
        catch (...) {
            fprintf(stderr,"[rank %d] worker thread unknown exception\n", my_rank);
            std::abort();
        }
    }

    /* ---------------- 单顶点处理 ---------------- */
    void processSingleVertex(unsigned vid, std::vector<unsigned>& resp) {
        const unsigned* nbr_data = nullptr;
        unsigned deg = 0;
        g->getLocalNeighbors(vid, nbr_data, deg);
        for (unsigned i = 0; i < deg; ++i)
            resp.push_back(nbr_data[i]);
    }

    /* ---------------- 成员 ---------------- */
    std::vector<std::thread> workers;
    BatchQueue               batchQueue;
    std::list<std::pair<std::vector<unsigned>, MPI_Request>> pendingSends_;
    std::mutex sendMtx_;

    void cleanCompletedSends() {
        std::lock_guard<std::mutex> lk(sendMtx_);
        auto it = pendingSends_.begin();
        while (it != pendingSends_.end()) {
            int flag = 0;
            MPI_Test(&it->second, &flag, MPI_STATUS_IGNORE);
            if (flag) {
                it = pendingSends_.erase(it);
            } else {
                ++it;
            }
        }
    }

public:
    std::thread main_thread{&Request::listen, this};
};

#endif   // REQUEST_H