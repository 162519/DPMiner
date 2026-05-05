// benchmark_vs_std.cpp
#include "/home/caohaoshuang/pjj/distributed_graph_query/PMiner-undirect/src/snapshot_pool.h"
#include <iostream>
#include <vector>
#include <thread>
#include <random>
#include <chrono>
#include <numeric>
#include <memory>

using namespace std::chrono;

// ========================
// 工具函数
// ========================

// 生成随机数组数据
std::vector<unsigned int> random_data(std::mt19937& gen, size_t min_sz = 4, size_t max_sz = 64) {
    std::uniform_int_distribution<> len_dist(min_sz, max_sz);
    std::uniform_int_distribution<> val_dist(1, 1000);

    size_t n = len_dist(gen);
    std::vector<unsigned int> arr(n);
    for (size_t i = 0; i < n; ++i) {
        arr[i] = val_dist(gen);
    }
    return arr;
}

// ========================
// 方法一：使用 PackedUintPool（你的方案）
// ========================
void test_packed_pool(size_t N) {
    PackedUintPool pool;
    std::vector<PackedUintPool::Handle> handles;
    handles.reserve(N);

    std::mt19937 gen(42);

    auto t0 = high_resolution_clock::now();

    // 分配
    for (size_t i = 0; i < N; ++i) {
        auto data = random_data(gen);
        auto h = pool.create(data.begin(), data.size());
        if (h) handles.push_back(h);
    }

    // 随机释放一半
    std::shuffle(handles.begin(), handles.end(), gen);
    for (size_t i = 0; i < handles.size() / 2; ++i) {
        pool.erase(handles[i]);
    }

    // 重新分配回来
    for (size_t i = 0; i < handles.size() / 2; ++i) {
        auto data = random_data(gen);
        auto h = pool.create(data.begin(), data.size());
        if (h) handles[i] = h;
    }

    auto t1 = high_resolution_clock::now();
    auto ms = duration_cast<milliseconds>(t1 - t0).count();

    std::cout << "✅ PackedUintPool: "
              << N << " ops in " << ms << " ms → "
              << (N * 1000.0 / ms) << " ops/s\n";
}

// ========================
// 方法二：使用 std::vector（“普通方法”）
// ========================
struct VectorWrapper {
    std::unique_ptr<unsigned int[]> data;
    size_t size;
    bool valid;

    VectorWrapper() : data(nullptr), size(0), valid(false) {}
    VectorWrapper(size_t sz) : size(sz), valid(true) {
        data = std::make_unique<unsigned int[]>(sz);
    }
};

void test_std_vector(size_t N) {
    std::vector<VectorWrapper> vectors;
    vectors.reserve(N);

    std::mt19937 gen(42);

    auto t0 = high_resolution_clock::now();

    // 分配
    for (size_t i = 0; i < N; ++i) {
        auto data = random_data(gen);
        VectorWrapper vec(data.size());
        std::memcpy(vec.data.get(), data.data(), data.size() * sizeof(unsigned int));
        vectors.push_back(std::move(vec));
    }

    // 随机释放一半
    std::shuffle(vectors.begin(), vectors.end(), gen);
    size_t to_free = vectors.size() / 2;
    vectors.erase(vectors.begin(), vectors.begin() + to_free);

    // 重新分配
    for (size_t i = 0; i < to_free; ++i) {
        auto data = random_data(gen);
        VectorWrapper vec(data.size());
        std::memcpy(vec.data.get(), data.data(), data.size() * sizeof(unsigned int));
        vectors.push_back(std::move(vec));
    }

    auto t1 = high_resolution_clock::now();
    auto ms = duration_cast<milliseconds>(t1 - t0).count();

    std::cout << "🔸 std::vector + new[]: "
              << N << " ops in " << ms << " ms → "
              << (N * 1000.0 / ms) << " ops/s\n";
}

// ========================
// 多线程版本对比
// ========================
void worker_packed(PackedUintPool& pool, size_t N, std::atomic<size_t>& count) {
    std::vector<PackedUintPool::Handle> local;
    local.reserve(N);
    std::mt19937 gen;

    for (size_t i = 0; i < N; ++i) {
        auto data = random_data(gen, 8, 32);
        auto h = pool.create(data.begin(), data.size());
        if (h) local.push_back(h);
    }

    std::shuffle(local.begin(), local.end(), gen);
    for (auto h : local) pool.erase(h);

    count += local.size();
}

void worker_std(size_t N, std::atomic<size_t>& count) {
    std::vector<std::unique_ptr<unsigned int[]>> ptrs;
    std::vector<size_t> sizes;
    ptrs.reserve(N);
    sizes.reserve(N);

    std::mt19937 gen;

    for (size_t i = 0; i < N; ++i) {
        auto data = random_data(gen, 8, 32);
        auto p = std::make_unique<unsigned int[]>(data.size());
        std::memcpy(p.get(), data.data(), data.size() * sizeof(unsigned int));
        ptrs.push_back(std::move(p));
        sizes.push_back(data.size());
    }

    count += ptrs.size();
    // 析构自动释放
}

void benchmark_multi_thread(size_t total_ops) {
    const size_t thread_count = std::thread::hardware_concurrency();
    const size_t per_thread = total_ops / thread_count;

    std::cout << "\n=== 多线程并发对比 (" << thread_count << " threads) ===\n";

    // Test 1: Packed Pool
    {
        PackedUintPool pool;
        std::atomic<size_t> count{0};
        std::vector<std::thread> ths;

        auto t0 = high_resolution_clock::now();
        for (int i = 0; i < thread_count; ++i) {
            ths.emplace_back(worker_packed, std::ref(pool), per_thread, std::ref(count));
        }
        for (auto& t : ths) t.join();
        auto t1 = high_resolution_clock::now();

        auto ms = duration_cast<milliseconds>(t1 - t0).count();
        std::cout << "✅ PackedUintPool MT: "
                  << count.load() << " ops in " << ms << " ms → "
                  << (count.load() * 1000.0 / ms) << " ops/s\n";
    }

    // Test 2: std::vector/new[]
    {
        std::atomic<size_t> count{0};
        std::vector<std::thread> ths;

        auto t0 = high_resolution_clock::now();
        for (int i = 0; i < thread_count; ++i) {
            ths.emplace_back(worker_std, per_thread, std::ref(count));
        }
        for (auto& t : ths) t.join();
        auto t1 = high_resolution_clock::now();

        auto ms = duration_cast<milliseconds>(t1 - t0).count();
        std::cout << "🔸 std::vector MT:     "
                  << count.load() << " ops in " << ms << " ms → "
                  << (count.load() * 1000.0 / ms) << " ops/s\n";
    }
}

// ========================
// 主函数
// ========================
int main() {
    const size_t N = 200'000;

    std::cout << "🎯 内存分配性能对比测试\n";
    std::cout << "数组大小: 4~64 个 unsigned int\n";
    std::cout << "总操作数: " << N << "\n\n";

    test_packed_pool(N);
    test_std_vector(N);

    benchmark_multi_thread(200'000);

    return 0;
}