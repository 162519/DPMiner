#include "/home/caohaoshuang/pjj/distributed_graph_query/PMiner-undirect/src/snapshot_pool.h"
#include <iostream>
#include <vector>
#include <thread>
#include <random>
#include <chrono>
#include <numeric>
#include <algorithm>

using namespace std::chrono;

// ========================
// 工具函数
// ========================

// 生成随机长度数组（3~100 个元素）
std::vector<unsigned int> random_array(std::mt19937& gen, size_t min_sz = 3, size_t max_sz = 100) {
    std::uniform_int_distribution<> len_dist(min_sz, max_sz);
    std::uniform_int_distribution<> val_dist(1, 1000);

    size_t n = len_dist(gen);
    std::vector<unsigned int> arr(n);
    for (size_t i = 0; i < n; ++i) {
        arr[i] = val_dist(gen);
    }
    return arr;
}

// 校验数据正确性
bool verify_data(const PackedUintPool& pool, PackedUintPool::Handle h, const std::vector<unsigned int>& original) {
    auto span = pool.get(h);
    if (span.size != original.size()) return false;
    return std::equal(span.begin(), span.end(), original.begin());
}

// ========================
// 测试 1：单线程基准性能
// ========================
void benchmark_single_thread() {
    std::cout << "\n=== 单线程性能测试 ===\n";

    PackedUintPool pool;
    const size_t N = 500'000;

    std::vector<PackedUintPool::Handle> handles;
    handles.reserve(N);

    std::mt19937 gen(42); // 固定种子便于对比

    auto start = high_resolution_clock::now();

    // 阶段 1：连续分配
    for (size_t i = 0; i < N; ++i) {
        auto data = random_array(gen);
        auto h = pool.create(data.begin(), data.size());
        if (h == PackedUintPool::kNullHandle) {
            std::cerr << "分配失败 at " << i << "\n";
            break;
        }
        if (!verify_data(pool, h, data)) {
            std::cerr << "数据错误 at " << i << "\n";
            break;
        }
        handles.push_back(h);
    }

    auto mid = high_resolution_clock::now();

    // 阶段 2：随机释放一半
    std::shuffle(handles.begin(), handles.end(), gen);
    size_t to_free = handles.size() / 2;
    for (size_t i = 0; i < to_free; ++i) {
        pool.erase(handles[i]);
    }

    // 阶段 3：重新分配同样数量
    size_t reallocated = 0;
    for (size_t i = 0; i < to_free; ++i) {
        auto data = random_array(gen);
        auto h = pool.create(data.begin(), data.size());
        if (h != PackedUintPool::kNullHandle && verify_data(pool, h, data)) {
            handles[to_free + i] = h;  // 覆盖旧 handle
            ++reallocated;
        }
    }

    auto end = high_resolution_clock::now();

    auto alloc_time   = duration_cast<milliseconds>(mid - start).count();
    auto realloc_time = duration_cast<milliseconds>(end - mid).count();
    auto total_time   = duration_cast<milliseconds>(end - start).count();

    std::cout << "总分配: " << N << "\n";
    std::cout << "释放并重分配: " << to_free << "\n";
    std::cout << "首次分配耗时: " << alloc_time << " ms (" << (N * 1000.0 / alloc_time) << " ops/s)\n";
    std::cout << "重分配耗时: " << realloc_time << " ms (" << (reallocated * 1000.0 / realloc_time) << " ops/s)\n";
    std::cout << "总耗时: " << total_time << " ms\n";
    std::cout << "最终页数: " << pool.size() << "\n";  // 注意：这里调用的是 size()（估算），非真实 page 数
}

// ========================
// 测试 2：多线程并发压测
// ========================
void worker(PackedUintPool& pool, size_t iter, std::atomic<size_t>& total_alloc, std::atomic<size_t>& total_free) {
    std::vector<PackedUintPool::Handle> local_handles;
    local_handles.reserve(iter);

    std::random_device rd;
    std::mt19937 gen(rd());

    for (size_t i = 0; i < iter; ++i) {
        auto data = random_array(gen, 4, 64);
        auto h = pool.create(data.begin(), data.size());
        if (h != PackedUintPool::kNullHandle) {
            local_handles.push_back(h);
            total_alloc.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // 随机释放一半
    std::shuffle(local_handles.begin(), local_handles.end(), gen);
    size_t to_free = local_handles.size() / 2;
    for (size_t i = 0; i < to_free; ++i) {
        pool.erase(local_handles[i]);
        total_free.fetch_add(1, std::memory_order_relaxed);
    }

    // 不归还给全局 handles → 让它留在池中或被其他线程复用
}

void benchmark_multi_thread() {
    std::cout << "\n=== 多线程并发测试 ===\n";

    PackedUintPool pool;
    constexpr size_t kThreads = 8;
    constexpr size_t kPerThread = 50'000;

    std::atomic<size_t> total_alloc{0}, total_free{0};
    std::vector<std::thread> threads;

    auto t0 = high_resolution_clock::now();

    for (size_t i = 0; i < kThreads; ++i) {
        threads.emplace_back(worker, std::ref(pool), kPerThread, std::ref(total_alloc), std::ref(total_free));
    }

    for (auto& t : threads) t.join();

    auto t1 = high_resolution_clock::now();

    auto ms = duration_cast<milliseconds>(t1 - t0).count();

    std::cout << "线程数: " << kThreads << "\n";
    std::cout << "每线程分配: " << kPerThread << "\n";
    std::cout << "总分配: " << total_alloc.load() << "\n";
    std::cout << "总释放: " << total_free.load() << "\n";
    std::cout << "吞吐量: " << (total_alloc.load() * 1000.0 / ms) << " ops/s\n";
    std::cout << "耗时: " << ms << " ms\n";
}

// ========================
// 测试 3：验证空洞复用行为（关键！）
// ========================
void test_hole_reuse() {
    std::cout << "\n=== 空洞复用行为检测 ===\n";

    PackedUintPool pool;

    // 创建三个小数组 A B C
    std::vector<unsigned int> A = {1, 2, 3, 4, 5};     // 5 elements
    std::vector<unsigned int> D = {6, 7, 8, 9};       // 4 elements —— 我们希望它复用 B 的位置

    auto hA = pool.create(A.begin(), A.size());
    auto hB = pool.create(A.begin(), A.size());  // same size
    auto hC = pool.create(A.begin(), A.size());

    std::cout << "A: page=" << hA.page_idx << ", offset=" << hA.offset << "\n";
    std::cout << "B: page=" << hB.page_idx << ", offset=" << hB.offset << "\n";
    std::cout << "C: page=" << hC.page_idx << ", offset=" << hC.offset << "\n";

    // 释放 B
    pool.erase(hB);

    // 再创建一个大小为 4 的数组 D
    auto hD = pool.create(D.begin(), D.size());

    std::cout << "D: page=" << hD.page_idx << ", offset=" << hD.offset << "\n";

    if (hD.page_idx == hB.page_idx && hD.offset == hB.offset) {
        std::cout << "[✅] D 成功复用了 B 的空间！\n";
    } else if (hD.page_idx == hB.page_idx && hD.offset > hB.offset) {
        std::cout << "[❌] D 没有复用 B，而是往后追加了 → 当前为 bump-pointer 模式\n";
    } else {
        std::cout << "[⚠️] D 分配到了不同页，无法判断\n";
    }
}

// ========================
// 测试 4：内存效率粗略评估
// ========================
void memory_efficiency_test() {
    std::cout << "\n=== 内存效率模拟 ===\n";

    PackedUintPool pool;
    std::vector<PackedUintPool::Handle> handles;

    const size_t N = 10000;
    std::mt19937 gen(123);

    // 分配大量小数组
    for (int i = 0; i < N; ++i) {
        auto arr = random_array(gen, 4, 16);
        auto h = pool.create(arr.begin(), arr.size());
        if (h != PackedUintPool::kNullHandle) {
            handles.push_back(h);
        }
    }

    // 释放奇数位
    for (int i = 0; i < handles.size(); i += 2) {
        pool.erase(handles[i]);
    }

    // 观察后续分配是否会复用？
    size_t reused = 0;
    size_t appended = 0;
    for (int i = 0; i < handles.size() / 2; ++i) {
        auto arr = random_array(gen, 4, 16);
        auto h = pool.create(arr.begin(), arr.size());
        if (h != PackedUintPool::kNullHandle) {
            // 这里无法直接知道是否复用了旧空间，但可通过 Page.used 趋势判断
            // （当前版本无接口暴露 used，仅作示意）
            handles[i % handles.size()] = h; // reuse slot
        }
    }

    std::cout << "共分配 " << N << " 个小数组\n";
    std::cout << "约释放了 " << N / 2 << " 个\n";
    std::cout << "当前池仍在运行...\n";
    std::cout << "(提示：可通过 Valgrind 或自定义 Page.used 监控实际内存增长)\n";
}

// ========================
// 主函数
// ========================
int main() {
    std::cout << "🎯 PackedUintPool 性能测试套件\n";

    benchmark_single_thread();
    benchmark_multi_thread();
    test_hole_reuse();
    memory_efficiency_test();

    return 0;
}