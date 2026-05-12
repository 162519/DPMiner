#ifndef RESPONSE_H
#define RESPONSE_H
#include "../util/global.h"
#include <thread>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <chrono>



// 1. 任务 = 一块应答
struct RespTask {
    std::vector<unsigned> buf;  // 已深拷
    int                   src;
};

// 2. 无锁队列
class RespTaskQueue {
public:
    void push(RespTask&& t) {
        { std::unique_lock<std::mutex> lk(m_); q_.emplace_back(std::move(t)); }
        cv_.notify_one();
    }
    bool pop(RespTask& t) {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [this]{ return !q_.empty() || stop_; });
        if (q_.empty()) return false;
        t = std::move(q_.front());
        q_.pop_front();
        return true;
    }
    void shutdown() {
        { std::unique_lock<std::mutex> lk(m_); stop_ = true; }
        cv_.notify_all();
    }
private:
    std::deque<RespTask>      q_;
    std::mutex                m_;
    std::condition_variable   cv_;
    bool                      stop_ = false;
};

// 3. Response 类
class Response {
    ThreadSlot* listenSlot_ = nullptr;
    ThreadSlot* workerSlot_ = nullptr;
public:
    Response() {
        listenSlot_ = Profiler::instance().registerThread("resp/listen");
        workerSlot_ = Profiler::instance().registerThread("resp/worker");
        int n = 1;
        workers.reserve(n);
        for (int i = 0; i < n; ++i)
            workers.emplace_back(&Response::workerLoop, this);
    }
    ~Response() {
        main_thread.join();
        taskQueue.shutdown();
        for (auto& t : workers) t.join();
    }

private:
void listen() {
    try{
    while (!isAllIdle) {
        int flag;  MPI_Status st;
        MPI_Iprobe(MPI_ANY_SOURCE, RESPONSE_MSG, MPI_COMM_WORLD, &flag, &st);
        if (!flag) { usleep(WAIT_TIME_WHEN_IDLE); continue; }

        ScopedTimer profL(Profiler::T_RESP_LISTEN);
        ScopedActive profAct(listenSlot_);
        Profiler::instance().incCounter(Profiler::C_RECVS);
        int totBytes;
        MPI_Get_count(&st, MPI_BYTE, &totBytes);
        if (totBytes < (int)sizeof(MsgHeader)) continue;

        std::vector<unsigned char> raw(totBytes);
        MPI_Recv(raw.data(), totBytes, MPI_BYTE, st.MPI_SOURCE,
                 RESPONSE_MSG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        MsgHeader hdr;
        std::memcpy(&hdr, raw.data(), sizeof(hdr));
        if (hdr.magic != 0xDEADBEEF || hdr.bytes + sizeof(hdr) != (size_t)totBytes) {
            fprintf(stderr, "Bad packet from %d, drop\n", st.MPI_SOURCE);
            continue;
        }

        size_t nWord = hdr.bytes / sizeof(unsigned);
        std::vector<unsigned> payload(nWord);
        std::memcpy(payload.data(), raw.data() + sizeof(hdr), hdr.bytes);
        taskQueue.push(RespTask{std::move(payload), st.MPI_SOURCE});
    }
}
catch (const std::exception& e) {
    fprintf(stderr,"[rank %d] Response::listen exception: %s\n", my_rank, e.what());
    std::abort();
}
catch (...) {
    fprintf(stderr,"[rank %d] Response::listen unknown exception\n", my_rank);
    std::abort();
}
}

    void workerLoop() {
        try{
        RespTask tk;
        while (taskQueue.pop(tk)) {
            ScopedTimer profW(Profiler::T_RESP_WORKER);
            ScopedActive profAct(workerSlot_);
            unsigned size = tk.buf[0];
            {
                ScopedTimer profUB(Profiler::T_UPDATE_BATCH);
                g->updata_batch(tk.buf.data(), tk.buf.size(), 1+size, size);
            }
            response_num_from_other[tk.src] += size;
            for (unsigned i = 0; i < size; ++i) {
                unsigned vid = tk.buf[i+1];
                bitmap->set(my_rank, vid, LOCAL);
            }
            std::lock_guard<std::mutex> lk(g_dataReadyMtx);
            g_dataReadyCv.notify_one();
        }
    }
    catch (const std::exception& e) {
        fprintf(stderr,"[rank %d] Response::workerLoop exception: %s\n", my_rank, e.what());
        std::abort();
    }
    catch (...) {
        fprintf(stderr,"[rank %d] Response::workerLoop unknown exception\n", my_rank);
        std::abort();
    }
    }

    std::vector<std::thread> workers;
    RespTaskQueue            taskQueue;

public:
    std::thread main_thread{&Response::listen, this};
};
#endif