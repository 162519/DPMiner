// test_performance.cpp
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <cstring>
#include "/home/caohaoshuang/pjj/distributed_graph_query/PMiner-undirect/src/BufferArena.h"

class PerformanceTester {
private:
    BufferArena& arena_;
    std::atomic<int64_t> total_operations_{0};
    std::atomic<int64_t> total_duration_{0}; // microseconds
    std::mutex cout_mutex_;
    
    void safe_print(const std::string& msg) {
        std::lock_guard<std::mutex> lock(cout_mutex_);
        std::cout << msg << std::endl;
    }

public:
    PerformanceTester(BufferArena& arena) : arena_(arena) {}

    void stress_worker(int thread_id, int operations, size_t min_size, size_t max_size) {
        auto start = std::chrono::high_resolution_clock::now();
        
        int success = 0;
        for (int i = 0; i < operations; ++i) {
            // 随机大小分配，模拟真实场景
            size_t size = min_size + (i % (max_size - min_size + 1));
            size_t align = (1 << (i % 4));  // 1, 2, 4, 8 字节对齐
            
            auto ptr = arena_.allocate(size, align);
            if (ptr) {
                // 简单写入验证
                std::memset(ptr.ptr, thread_id, ptr.size);
                success++;
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        total_operations_ += success;
        total_duration_ += duration;
        
        safe_print("线程" + std::to_string(thread_id) + 
                  " 完成: " + std::to_string(success) + "/" + std::to_string(operations) +
                  " 操作, 耗时: " + std::to_string(duration / 1000.0) + "ms");
    }

    void run_performance_test(int num_threads, int operations_per_thread, 
                             size_t min_size, size_t max_size) {
        safe_print("\n🎯 性能测试开始");
        safe_print("========================================");
        safe_print("线程数: " + std::to_string(num_threads));
        safe_print("每线程操作数: " + std::to_string(operations_per_thread));
        safe_print("分配大小范围: " + std::to_string(min_size) + " - " + std::to_string(max_size) + " bytes");
        safe_print("总操作数: " + std::to_string(num_threads * operations_per_thread));
        safe_print("初始状态 - 页数: " + std::to_string(arena_.page_count()) + 
                  ", 内存效率: " + std::to_string(arena_.memory_efficiency() * 100) + "%");
        
        total_operations_ = 0;
        total_duration_ = 0;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back(&PerformanceTester::stress_worker, this, 
                               i, operations_per_thread, min_size, max_size);
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto total_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        // 性能统计
        safe_print("========================================");
        safe_print("📊 性能报告:");
        safe_print("成功操作: " + std::to_string(total_operations_.load()));
        safe_print("最终页数: " + std::to_string(arena_.page_count()));
        safe_print("总内存: " + std::to_string(arena_.total_reserved() / 1024) + " KB");
        safe_print("内存效率: " + std::to_string(arena_.memory_efficiency() * 100) + "%");
        safe_print("总耗时: " + std::to_string(total_time / 1000.0) + " ms");
        
        double avg_latency = static_cast<double>(total_duration_.load()) / total_operations_;
        double throughput = total_operations_ * 1000000.0 / total_time;
        
        safe_print("平均延迟: " + std::to_string(avg_latency) + " μs/操作");
        safe_print("吞吐量: " + std::to_string(throughput) + " 操作/ms");
        safe_print("QPS: " + std::to_string(throughput * 1000) + " 操作/秒");
        safe_print("✅ 性能测试完成");
    }
};

int main() {
    try {
        BufferArena arena;
        PerformanceTester tester(arena);
        
        // 不同场景的性能测试
        std::cout << "🚀 开始高性能多线程测试..." << std::endl;
        
        // 场景1: 小对象高并发
        tester.run_performance_test(8, 50000, 8, 64);
        
        // 场景2: 中等对象中等并发
        tester.run_performance_test(4, 100000, 64, 256);
        
        // 场景3: 混合大小高负载
        tester.run_performance_test(16, 25000, 16, 512);
        
        std::cout << "\n🎉 所有性能测试完成!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "测试异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}