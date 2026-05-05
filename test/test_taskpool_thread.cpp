#include "/home/caohaoshuang/pjj/distributed_graph_query/PMiner-undirect/src/task_pool.h"
#include "/home/caohaoshuang/pjj/distributed_graph_query/PMiner-undirect/src/Task.h"
#include <iostream>
#include <vector>
#include <thread>
#include <random>
#include <chrono>
#include <algorithm>

using namespace std::chrono;

constexpr size_t kThreads = 28;
constexpr size_t kPerThread = 50'000;
constexpr size_t kBatchFree = kPerThread / 2;

struct ThreadResult {
    size_t allocated = 0;
    size_t freed = 0;
    size_t reused = 0;
    size_t conflict = 0;
};

void worker(SmallPool<Task>& pool, ThreadResult& out) {
    ThreadResult& r = out;
    std::vector<unsigned int> idx;
    idx.reserve(kPerThread);

    // 1. 分配
    for (size_t i = 0; i < kPerThread; ++i) {
        unsigned int id = pool.allocate();
        if (id == unsigned(-1)) break;
        Task& t = pool.at(id);
        idx.push_back(id);
    }
    r.allocated = idx.size();

    // 2. 随机释放一半
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(idx.begin(), idx.end(), gen);
    std::vector<unsigned int> freed(idx.begin(), idx.begin() + kBatchFree);
    for (unsigned int id : freed) pool.free(id);
    r.freed = freed.size();

    // 3. 立即重分配
    std::sort(freed.begin(), freed.end());
    for (size_t i = 0; i < kBatchFree; ++i) {
        unsigned int id = pool.allocate();
        if (id == unsigned(-1)) break;
        if (std::binary_search(freed.begin(), freed.end(), id)) {
            ++r.reused;
        } else {
            ++r.conflict;
        }
    }
}

int main() {
    SmallPool<Task> pool;

    std::vector<ThreadResult> results(kThreads);
    std::vector<std::thread> threads;

    auto t0 = high_resolution_clock::now();

    for (size_t i = 0; i < kThreads; ++i) {
        threads.emplace_back(worker, std::ref(pool), std::ref(results[i]));
    }
    for (auto& t : threads) t.join();

    auto t1 = high_resolution_clock::now();

    size_t total_alloc = 0, total_free = 0, total_reused = 0, total_conflict = 0;
    for (const auto& r : results) {
        total_alloc += r.allocated;
        total_free += r.freed;
        total_reused += r.reused;
        total_conflict += r.conflict;
    }

    std::cout << "========= 多线程测试 =========\n";
    std::cout << "线程数        : " << kThreads << "\n";
    std::cout << "每线程分配数  : " << kPerThread << "\n";
    std::cout << "总分配        : " << total_alloc << "\n";
    std::cout << "总释放        : " << total_free << "\n";
    std::cout << "总重分配      : " << (total_reused + total_conflict) << "\n";
    std::cout << "重用自己块    : " << total_reused << "\n";
    std::cout << "与别人冲突    : " << total_conflict << "\n";
    std::cout << "内存效率      : " << pool.memory_efficiency() * 100 << "%\n";
    std::cout << "耗时          : " << duration_cast<milliseconds>(t1 - t0).count() << " ms\n";
    return 0;
}