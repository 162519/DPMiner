#pragma once
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <vector>
#include <string>
#include <mutex>

enum class ThreadState : int {
    ACTIVE  = 0,
    IDLE    = 1,
    WAITING = 2,
    COUNT   = 3
};

inline const char* threadStateName(ThreadState s) {
    static const char* names[] = {"ACTIVE", "IDLE", "WAITING"};
    return names[static_cast<int>(s)];
}

struct alignas(64) ThreadSlot {
    std::atomic<ThreadState> state{ThreadState::IDLE};
    std::chrono::steady_clock::time_point lastTransition;
    std::atomic<long long> activeTimeUs{0};
    std::atomic<long long> idleTimeUs{0};
    std::atomic<long long> waitTimeUs{0};

    void transition(ThreadState newState) {
        auto now = std::chrono::steady_clock::now();
        ThreadState oldState = state.load(std::memory_order_relaxed);
        long long elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(now - lastTransition).count();
        if (elapsedUs < 0) elapsedUs = 0;
        switch (oldState) {
            case ThreadState::ACTIVE:  activeTimeUs.fetch_add(elapsedUs, std::memory_order_relaxed); break;
            case ThreadState::IDLE:    idleTimeUs.fetch_add(elapsedUs, std::memory_order_relaxed); break;
            case ThreadState::WAITING: waitTimeUs.fetch_add(elapsedUs, std::memory_order_relaxed); break;
            default: break;
        }
        lastTransition = now;
        state.store(newState, std::memory_order_relaxed);
    }

    void finalize() {
        transition(ThreadState::IDLE);
    }

    void getPercentages(double& activePct, double& idlePct, double& waitPct) const {
        long long a = activeTimeUs.load(std::memory_order_relaxed);
        long long i = idleTimeUs.load(std::memory_order_relaxed);
        long long w = waitTimeUs.load(std::memory_order_relaxed);
        long long total = a + i + w;
        if (total <= 0) total = 1;
        activePct = (double)a / total * 100;
        idlePct = (double)i / total * 100;
        waitPct = (double)w / total * 100;
    }

    double getActiveTime() const {
        return activeTimeUs.load(std::memory_order_relaxed) / 1000000.0;
    }
};

class Profiler {
public:
    static Profiler& instance() {
        static Profiler p;
        return p;
    }

    ThreadSlot* registerThread(const std::string& name) {
        std::lock_guard<std::mutex> lk(mtx_);
        int idx = nextSlot_++;
        slots_[idx].lastTransition = std::chrono::steady_clock::now();
        infos_.push_back({name, &slots_[idx]});
        return &slots_[idx];
    }

    void report() {
        for (auto& info : infos_) {
            info.slot->finalize();
        }

        double elapsed = since(startTs_);
        if (elapsed <= 0) elapsed = 1.0;

        printf("\n========== [rank %d] Performance Profile ==========\n", rank_);
        printf("Total elapsed: %.3f s\n\n", elapsed);

        printf("--- Thread Activity ---\n");
        printf("%-35s  %8s  %8s  %8s  %8s\n", "Thread", "Active%", "Idle%", "Wait%", "Time(s)");
        for (auto& info : infos_) {
            double aPct, iPct, wPct;
            info.slot->getPercentages(aPct, iPct, wPct);
            double aTime = info.slot->getActiveTime();
            printf("%-35s  %7.1f%%  %7.1f%%  %7.1f%%  %8.3f\n",
                   info.name.c_str(), aPct, iPct, wPct, aTime);
        }

        printf("\n--- Critical Path Timing ---\n");
        printf("%-45s  %12s  %5s\n", "Phase", "Time(s)", "Pct%");

        auto printTimer = [&](const char* name, long long us) {
            double t = us / 1000000.0;
            printf("%-45s  %12.3f  %5.1f\n", name, t, t / elapsed * 100);
        };

        printTimer("searchALLPR (pipeline loop)",              timers_[T_SEARCH_ALL].load());
        printTimer("  collectBatchFromQueues",                  timers_[T_COLLECT_BATCH].load());
        printTimer("  analyzeBatchRemoteVids",                  timers_[T_ANALYZE_REMOTE].load());
        printTimer("  getRemoteData (send requests)",           timers_[T_GET_REMOTE].load());
        printTimer("  waitForReady (data wait)",                timers_[T_WAIT_READY].load());
        printTimer("  dispatchBatchToPool",                     timers_[T_DISPATCH].load());
        printTimer("searchPG (per-task compute)",               timers_[T_SEARCH_PG].load());
        printTimer("  extendEdgePattern",                       timers_[T_EXTEND].load());
        printTimer("  extendEdgePattern_final",                 timers_[T_EXTEND_FINAL].load());
        printTimer("updata_batch (graph data update)",          timers_[T_UPDATE_BATCH].load());
        printTimer("Request::listen (recv req)",                timers_[T_REQ_LISTEN].load());
        printTimer("Request::workerLoop (process+send resp)",   timers_[T_REQ_WORKER].load());
        printTimer("Response::listen (recv resp)",              timers_[T_RESP_LISTEN].load());
        printTimer("Response::workerLoop (update local)",       timers_[T_RESP_WORKER].load());

        printf("\n--- Counters ---\n");
        printf("%-45s  %llu\n", "totalTaskCount",      (unsigned long long)counters_[C_TOTAL_TASKS].load());
        printf("%-45s  %llu\n", "remoteDataRequests",   (unsigned long long)counters_[C_REMOTE_REQS].load());
        printf("%-45s  %llu\n", "batchesDispatched",    (unsigned long long)counters_[C_BATCHES].load());
        printf("%-45s  %llu\n", "mpiIsendCount",        (unsigned long long)counters_[C_ISENDS].load());
        printf("%-45s  %llu\n", "mpiRecvCount",         (unsigned long long)counters_[C_RECVS].load());
        printf("===================================================\n\n");
    }

    void setRank(int r) {
        rank_ = r;
        startTs_ = std::chrono::steady_clock::now();
    }

    enum TimerIdx {
        T_SEARCH_ALL = 0,
        T_COLLECT_BATCH,
        T_ANALYZE_REMOTE,
        T_GET_REMOTE,
        T_WAIT_READY,
        T_DISPATCH,
        T_SEARCH_PG,
        T_EXTEND,
        T_EXTEND_FINAL,
        T_UPDATE_BATCH,
        T_REQ_LISTEN,
        T_REQ_WORKER,
        T_RESP_LISTEN,
        T_RESP_WORKER,
        T_COUNT
    };

    enum CounterIdx {
        C_TOTAL_TASKS = 0,
        C_REMOTE_REQS,
        C_BATCHES,
        C_ISENDS,
        C_RECVS,
        C_COUNT
    };

    void addTime(TimerIdx idx, double seconds) {
        long long us = (long long)(seconds * 1000000);
        timers_[idx].fetch_add(us, std::memory_order_relaxed);
    }

    void incCounter(CounterIdx idx, unsigned long long n = 1) {
        counters_[idx].fetch_add(n, std::memory_order_relaxed);
    }

private:
    Profiler() = default;

    static double since(std::chrono::steady_clock::time_point start) {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - start).count();
    }

    int rank_ = 0;
    std::chrono::steady_clock::time_point startTs_;
    std::mutex mtx_;
    int nextSlot_ = 0;

    struct ThreadInfo {
        std::string name;
        ThreadSlot* slot;
    };
    std::vector<ThreadInfo> infos_;

    static constexpr int MAX_SLOTS = 32;
    ThreadSlot slots_[MAX_SLOTS];

    std::atomic<long long> timers_[T_COUNT]{};
    std::atomic<unsigned long long> counters_[C_COUNT]{};
};

class ScopedTimer {
public:
    ScopedTimer(Profiler::TimerIdx idx)
        : idx_(idx), start_(std::chrono::steady_clock::now()) {}
    ~ScopedTimer() {
        double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start_).count();
        Profiler::instance().addTime(idx_, elapsed);
    }
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
private:
    Profiler::TimerIdx idx_;
    std::chrono::steady_clock::time_point start_;
};

class ScopedActive {
public:
    explicit ScopedActive(ThreadSlot* s) : slot_(s) {
        slot_->transition(ThreadState::ACTIVE);
    }
    ~ScopedActive() {
        slot_->transition(ThreadState::IDLE);
    }
    ScopedActive(const ScopedActive&) = delete;
    ScopedActive& operator=(const ScopedActive&) = delete;
private:
    ThreadSlot* slot_;
};

class ScopedWait {
public:
    explicit ScopedWait(ThreadSlot* s) : slot_(s) {
        slot_->transition(ThreadState::WAITING);
    }
    ~ScopedWait() {
        slot_->transition(ThreadState::IDLE);
    }
    ScopedWait(const ScopedWait&) = delete;
    ScopedWait& operator=(const ScopedWait&) = delete;
private:
    ThreadSlot* slot_;
};
