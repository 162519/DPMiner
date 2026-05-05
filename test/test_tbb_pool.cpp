#include "/home/caohaoshuang/pjj/distributed_graph_query/PMiner-undirect/src/task_pool.h"  // 假设你的类在这个头文件中
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <random>
#include <cassert>
#include <mutex>
#include <algorithm>

// 32字节的测试TASK结构体
struct TestTask {
    uint64_t id;
    uint64_t data1;
    uint64_t data2;
    uint64_t data3;
    
    TestTask() : id(0), data1(0), data2(0), data3(0) {}
    
    void init(uint64_t task_id) {
        id = task_id;
        data1 = task_id * 2;
        data2 = task_id * 3;
        data3 = task_id * 4;
    }
    
    bool validate() const {
        return data1 == id * 2 && 
               data2 == id * 3 && 
               data3 == id * 4;
    }
};

// 测试1: 基本功能测试
void test_basic_functionality() {
    std::cout << "\n=== 测试1: 基本功能测试 ===" << std::endl;
    
    ConcurrentTaskPool<TestTask> pool;
    
    // 分配几个TASK
    size_t idx1 = pool.allocate();
    size_t idx2 = pool.allocate();
    size_t idx3 = pool.allocate();
    
    std::cout << "分配索引: " << idx1 << ", " << idx2 << ", " << idx3 << std::endl;
    
    // 初始化TASK
    pool.at(idx1).init(1001);
    pool.at(idx2).init(1002);
    pool.at(idx3).init(1003);
    
    // 验证数据
    assert(pool.at(idx1).id == 1001);
    assert(pool.at(idx2).id == 1002);
    assert(pool.at(idx3).id == 1003);
    assert(pool.at(idx1).validate());
    assert(pool.at(idx2).validate());
    assert(pool.at(idx3).validate());
    
    // 检查状态
    assert(pool.is_allocated(idx1));
    assert(pool.is_allocated(idx2));
    assert(pool.is_allocated(idx3));
    assert(pool.active_count() == 3);
    
    // 释放TASK
    pool.free(idx2);
    assert(!pool.is_allocated(idx2));
    assert(pool.active_count() == 2);
    
    // 重新分配应该重用idx2
    size_t idx4 = pool.allocate();
    std::cout << "重新分配索引: " << idx4 << " (期望是 " << idx2 << ")" << std::endl;
    assert(idx4 == idx2);
    
    // 验证新分配的TASK已经被重置
    assert(pool.at(idx4).id == 0);
    
    // 清理
    pool.free(idx1);
    pool.free(idx3);
    pool.free(idx4);
    
    std::cout << "✓ 基本功能测试通过" << std::endl;
}

// 测试2: 批量操作测试
void test_batch_operations() {
    std::cout << "\n=== 测试2: 批量操作测试 ===" << std::endl;
    
    ConcurrentTaskPool<TestTask> pool;
    const size_t batch_size = 100;
    size_t indices[batch_size];
    
    // 批量分配
    pool.allocate_batch(indices, batch_size);
    
    std::cout << "批量分配了 " << batch_size << " 个TASK" << std::endl;
    std::cout << "活动计数: " << pool.active_count() << std::endl;
    
    // 初始化所有TASK
    for (size_t i = 0; i < batch_size; ++i) {
        pool.at(indices[i]).init(2000 + i);
    }
    
    // 验证所有TASK
    for (size_t i = 0; i < batch_size; ++i) {
        assert(pool.at(indices[i]).id == 2000 + i);
        assert(pool.at(indices[i]).validate());
    }
    
    // 批量释放一半
    pool.free_batch(indices, batch_size / 2);
    std::cout << "释放了前一半，活动计数: " << pool.active_count() << std::endl;
    
    // 验证状态
    for (size_t i = 0; i < batch_size / 2; ++i) {
        assert(!pool.is_allocated(indices[i]));
    }
    for (size_t i = batch_size / 2; i < batch_size; ++i) {
        assert(pool.is_allocated(indices[i]));
    }
    
    // 重新分配应该重用释放的索引
    size_t new_indices[batch_size / 2];
    pool.allocate_batch(new_indices, batch_size / 2);
    
    // 清理
    pool.free_batch(&indices[batch_size / 2], batch_size / 2);
    pool.free_batch(new_indices, batch_size / 2);
    
    std::cout << "✓ 批量操作测试通过" << std::endl;
}

// 测试3: 多线程并发测试（简化版）
void test_multithreaded_concurrent() {
    std::cout << "\n=== 测试3: 多线程并发测试 ===" << std::endl;
    
    ConcurrentTaskPool<TestTask> pool;
    const size_t num_threads = 4;
    const size_t operations_per_thread = 10000;
    
    std::atomic<size_t> success_count{0};
    std::atomic<size_t> error_count{0};
    
    auto worker = [&](size_t thread_id) {
        std::vector<size_t> indices;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1, 10);
        
        for (size_t i = 0; i < operations_per_thread; ++i) {
            if (dis(gen) <= 7 || indices.empty()) {
                // 分配
                size_t idx = pool.allocate();
                indices.push_back(idx);
                
                try {
                    pool.at(idx).init(thread_id * 1000000 + i);
                    success_count.fetch_add(1);
                } catch (...) {
                    error_count.fetch_add(1);
                }
            } else {
                // 释放
                size_t idx = indices.back();
                indices.pop_back();
                
                try {
                    pool.free(idx);
                    success_count.fetch_add(1);
                } catch (...) {
                    error_count.fetch_add(1);
                }
            }
        }
        
        // 清理剩余的TASK
        for (size_t idx : indices) {
            pool.free(idx);
        }
    };
    
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> threads;
    
    for (size_t i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "统计信息:" << std::endl;
    std::cout << "  线程数: " << num_threads << std::endl;
    std::cout << "  每线程操作数: " << operations_per_thread << std::endl;
    std::cout << "  成功操作数: " << success_count.load() << std::endl;
    std::cout << "  错误操作数: " << error_count.load() << std::endl;
    std::cout << "  耗时: " << duration.count() << "ms" << std::endl;
    std::cout << "  吞吐量: " << (num_threads * operations_per_thread * 1000.0 / duration.count()) << " ops/sec" << std::endl;
    std::cout << "  最终活动计数: " << pool.active_count() << std::endl;
    
    if (error_count.load() == 0 && pool.active_count() == 0) {
        std::cout << "✓ 多线程测试通过" << std::endl;
    } else {
        std::cout << "⚠ 多线程测试完成，但有 " << error_count.load() 
                  << " 个错误，剩余 " << pool.active_count() << " 个活动TASK" << std::endl;
    }
}

// 测试4: 边界条件测试
void test_edge_cases() {
    std::cout << "\n=== 测试4: 边界条件测试 ===" << std::endl;
    
    ConcurrentTaskPool<TestTask> pool;
    
    // 测试无效索引访问
    try {
        pool.at(999999);  // 应该抛出异常
        assert(false && "应该抛出异常");
    } catch (const std::out_of_range& e) {
        std::cout << "捕获到预期异常: " << e.what() << std::endl;
    }
    
    // 测试重复释放
    size_t idx = pool.allocate();
    pool.free(idx);
    pool.free(idx);  // 第二次释放应该静默失败
    
    // 测试预分配空间
    ConcurrentTaskPool<TestTask> preallocated_pool(100000);
    std::cout << "预分配了10万个槽位" << std::endl;
    
    // 测试大量分配
    const size_t large_count = 10000;
    std::vector<size_t> large_indices;
    large_indices.reserve(large_count);
    
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < large_count; ++i) {
        large_indices.push_back(pool.allocate());
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "分配 " << large_count << " 个TASK耗时: " 
              << duration.count() << "μs" << std::endl;
    std::cout << "平均每个分配: " 
              << (static_cast<double>(duration.count()) / large_count) << "μs" << std::endl;
    
    // 清理
    pool.free_batch(large_indices.data(), large_indices.size());
    
    std::cout << "✓ 边界条件测试通过" << std::endl;
}

// 测试5: 性能基准测试
void test_performance_benchmark() {
    std::cout << "\n=== 测试5: 性能基准测试 ===" << std::endl;
    
    const size_t num_operations = 100000;
    ConcurrentTaskPool<TestTask> pool;
    
    // 测试单线程分配/释放性能
    std::cout << "\n单线程性能测试 (" << num_operations << " 次操作):" << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < num_operations; ++i) {
        size_t idx = pool.allocate();
        pool.at(idx).init(i);
        pool.free(idx);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "总耗时: " << duration.count() << "μs" << std::endl;
    std::cout << "每次分配+释放: " 
              << (static_cast<double>(duration.count()) / num_operations) << "μs" << std::endl;
    std::cout << "吞吐量: " 
              << (num_operations * 1000000.0 / duration.count()) << " ops/sec" << std::endl;
    
    // 测试纯分配性能
    std::cout << "\n纯分配性能测试:" << std::endl;
    
    std::vector<size_t> indices;
    indices.reserve(num_operations);
    
    start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < num_operations; ++i) {
        indices.push_back(pool.allocate());
    }
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "分配 " << num_operations << " 个TASK耗时: " << duration.count() << "μs" << std::endl;
    std::cout << "平均每个分配: " 
              << (static_cast<double>(duration.count()) / num_operations) << "μs" << std::endl;
    
    // 测试纯释放性能
    std::cout << "\n纯释放性能测试:" << std::endl;
    
    start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < num_operations; ++i) {
        pool.free(indices[i]);
    }
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "释放 " << num_operations << " 个TASK耗时: " << duration.count() << "μs" << std::endl;
    std::cout << "平均每个释放: " 
              << (static_cast<double>(duration.count()) / num_operations) << "μs" << std::endl;
    
    std::cout << "\n✓ 性能基准测试完成" << std::endl;
}

// 主测试函数
int main() {
    std::cout << "开始测试 ConcurrentTaskPool..." << std::endl;
    
    try {
        test_basic_functionality();
        test_batch_operations();
        test_multithreaded_concurrent();
        test_edge_cases();
        test_performance_benchmark();
        
        std::cout << "\n==========================================" << std::endl;
        std::cout << "所有测试完成！" << std::endl;
        std::cout << "==========================================" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "\n测试失败: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}