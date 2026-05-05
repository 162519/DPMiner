// test_comprehensive.cpp
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <cstring>
#include <random>
#include "/home/caohaoshuang/pjj/distributed_graph_query/PMiner-undirect/src/BufferArena.h"

class ComprehensiveTester {
private:
    BufferArena& arena_;
    std::mutex cout_mutex_;
    
    void log(const std::string& msg) {
        std::lock_guard<std::mutex> lock(cout_mutex_);
        std::cout << msg << std::endl;
    }

public:
    ComprehensiveTester(BufferArena& arena) : arena_(arena) {}

    // 测试1: 基础功能测试
    void test_basic_functionality() {
        log("\n🔧 基础功能测试");
        log("========================================");
        
        std::vector<BufferArena::FatPtr> allocations;
        const int test_count = 1000;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // 分配不同大小的内存
        for (int i = 0; i < test_count; ++i) {
            size_t size = 8 + (i % 64);
            size_t align = 1 << (i % 4);
            
            auto ptr = arena_.allocate(size, align);
            if (ptr) {
                // 写入测试数据
                std::memset(ptr.ptr, i & 0xFF, ptr.size);
                allocations.push_back(ptr);
            } else {
                log("分配失败: size=" + std::to_string(size) + ", align=" + std::to_string(align));
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        log("基础测试完成: " + std::to_string(allocations.size()) + "/" + 
            std::to_string(test_count) + " 分配成功");
        log("耗时: " + std::to_string(duration / 1000.0) + " ms");
        log("内存效率: " + std::to_string(arena_.memory_efficiency() * 100) + "%");
    }

    // 测试2: 多线程竞争测试
    void test_concurrent_competition(int num_threads, int operations_per_thread) {
        log("\n⚡ 多线程竞争测试");
        log("========================================");
        log("线程数: " + std::to_string(num_threads));
        log("每线程操作数: " + std::to_string(operations_per_thread));
        
        std::atomic<int64_t> total_success{0};
        std::atomic<int64_t> total_duration{0};
        
        auto start = std::chrono::high_resolution_clock::now();
        
        std::vector<std::thread> threads;
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&, t]() {
                auto thread_start = std::chrono::high_resolution_clock::now();
                int success = 0;
                
                for (int i = 0; i < operations_per_thread; ++i) {
                    size_t size = 16 + ((t + i) % 128);
                    auto ptr = arena_.allocate(size, 8);
                    if (ptr) {
                        std::memset(ptr.ptr, t, ptr.size);
                        success++;
                    }
                }
                
                auto thread_end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                    thread_end - thread_start).count();
                
                total_success += success;
                total_duration += duration;
                
                log("线程" + std::to_string(t) + " 完成: " + 
                    std::to_string(success) + " 次分配");
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto total_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        log("竞争测试结果:");
        log("总成功分配: " + std::to_string(total_success.load()));
        log("总耗时: " + std::to_string(total_time / 1000.0) + " ms");
        log("吞吐量: " + std::to_string(total_success * 1000000.0 / total_time) + " 操作/ms");
    }

    // 测试3: 内存碎片测试
    void test_fragmentation() {
        log("\n🧩 内存碎片测试");
        log("========================================");
        
        std::vector<BufferArena::FatPtr> allocations;
        const int cycles = 5;
        const int per_cycle = 2000;
        
        for (int cycle = 0; cycle < cycles; ++cycle) {
            // 分配阶段
            for (int i = 0; i < per_cycle; ++i) {
                size_t size = 32 + (i % 96);
                auto ptr = arena_.allocate(size, 8);
                if (ptr) {
                    allocations.push_back(ptr);
                }
            }
            
            log("周期 " + std::to_string(cycle + 1) + 
                ": 已分配 " + std::to_string(allocations.size()) + 
                " 对象, 内存效率: " + 
                std::to_string(arena_.memory_efficiency() * 100) + "%");
        }
        
        log("碎片测试完成，总对象数: " + std::to_string(allocations.size()));
    }

    // 测试4: 随机压力测试
    void test_random_stress(int num_threads, int duration_seconds) {
        log("\n🎲 随机压力测试");
        log("========================================");
        log("线程数: " + std::to_string(num_threads));
        log("持续时间: " + std::to_string(duration_seconds) + " 秒");
        
        std::atomic<bool> stop{false};
        std::atomic<int64_t> total_operations{0};
        std::vector<std::thread> threads;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // 启动工作线程
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&, t]() {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<size_t> size_dist(8, 512);
                std::uniform_int_distribution<size_t> align_dist(0, 3);
                
                int operations = 0;
                while (!stop.load()) {
                    size_t size = size_dist(gen);
                    size_t align = 1 << align_dist(gen);
                    
                    auto ptr = arena_.allocate(size, align);
                    if (ptr) {
                        operations++;
                    }
                }
                total_operations += operations;
            });
        }
        
        // 运行指定时间
        std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));
        stop.store(true);
        
        for (auto& t : threads) {
            t.join();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto total_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        log("压力测试结果:");
        log("总操作数: " + std::to_string(total_operations.load()));
        log("运行时间: " + std::to_string(total_time / 1000000.0) + " 秒");
        log("QPS: " + std::to_string(total_operations * 1000000.0 / total_time));
        log("最终页数: " + std::to_string(arena_.page_count()));
        log("内存效率: " + std::to_string(arena_.memory_efficiency() * 100) + "%");
    }

    // 运行所有测试
    void run_all_tests() {
        log("🚀 开始全面测试 BufferArena");
        
        test_basic_functionality();
        test_concurrent_competition(8, 10000);
        test_fragmentation();
        test_random_stress(4, 5);  // 4线程运行5秒
        
        log("\n🎉 所有测试完成!");
    }
};

int main() {
    try {
        BufferArena arena;
        ComprehensiveTester tester(arena);
        tester.run_all_tests();
    } catch (const std::exception& e) {
        std::cerr << "测试异常: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}