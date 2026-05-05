#include "/home/caohaoshuang/pjj/distributed_graph_query/PMiner-undirect/src/priority_task_scheduler.h"
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <random>
#include <iomanip>

using namespace std::chrono;

int main() {
    std::cout << std::fixed << std::setprecision(2);

    ConcurrentPriorityScheduler scheduler;
    const int NUM_PRODUCERS = 28;
    const int TASKS_PER_PRODUCER = 100'000;
    const int NUM_CONSUMERS = 4;
    const size_t BATCH_SIZE = 64;

    // === 记录开始时间 ===
    auto start_time = high_resolution_clock::now();

    // === 统计变量 ===
    std::atomic<size_t> submitted{0}, processed{0};
    std::atomic<size_t> total_batches_popped{0};  // 批次数
    std::atomic<size_t> total_tasks_popped{0};   // 实际处理的任务数

    // 用于随机生成 depth
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> depth_dist(1, 10);

    // === 生产者线程：提交任务 ===
    std::vector<std::thread> producers;
    auto producer_start = high_resolution_clock::now();

    for (int t = 0; t < NUM_PRODUCERS; ++t) {
        producers.emplace_back([&](int tid) {
            for (int i = 0; i < TASKS_PER_PRODUCER; ++i) {
                uint8_t depth = depth_dist(gen);
                uint32_t idx = tid * 100000 + i;
                scheduler.submit({depth, idx});
                submitted.fetch_add(1, std::memory_order_relaxed);
            }
        }, t);
    }

    // 等待所有生产者完成
    for (auto& t : producers) t.join();
    auto producer_end = high_resolution_clock::now();

    // === 消费者线程：批量取出并处理 ===
    std::vector<std::thread> consumers;
    auto consumer_start = high_resolution_clock::now();

    for (int c = 0; c < NUM_CONSUMERS; ++c) {
        consumers.emplace_back([&]() {
            std::vector<TaskHandle> batch;
            batch.reserve(BATCH_SIZE);

            while (processed.load(std::memory_order_acquire) < 
                   NUM_PRODUCERS * TASKS_PER_PRODUCER) {

                if (scheduler.try_pop_batch(batch, BATCH_SIZE)) {
                    total_batches_popped.fetch_add(1, std::memory_order_relaxed);
                    size_t n = batch.size();
                    total_tasks_popped.fetch_add(n, std::memory_order_relaxed);

                    // 模拟处理时间（可选）
                    // for (auto& h : batch) { /* process */ }

                    processed.fetch_add(n, std::memory_order_release);
                } else {
                    // 小休一下，避免空转
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            }
        });
    }

    // 等待所有消费者完成
    for (auto& t : consumers) t.join();
    auto consumer_end = high_resolution_clock::now();

    // === 结束时间 ===
    auto end_time = high_resolution_clock::now();

    // === 计算耗时 ===
    auto total_ms      = duration_cast<milliseconds>(end_time - start_time).count();
    auto prod_ms       = duration_cast<milliseconds>(producer_end - producer_start).count();
    auto consume_ms    = duration_cast<milliseconds>(consumer_end - consumer_start).count();
    auto peak_memory   = scheduler.size();  // 调度器最终剩余（应为0）

    // === 输出性能报告 ===
    std::cout << "\n📊 性能监控报告\n";
    std::cout << "==============================\n";
    std::cout << "生产者线程数     : " << NUM_PRODUCERS << "\n";
    std::cout << "消费者线程数     : " << NUM_CONSUMERS << "\n";
    std::cout << "每生产者任务数   : " << TASKS_PER_PRODUCER << "\n";
    std::cout << "总提交任务数     : " << submitted.load() << "\n";
    std::cout << "总处理任务数     : " << processed.load() << "\n";

    std::cout << "\n⏱️ 时间消耗\n";
    std::cout << "总运行时间       : " << total_ms << " ms\n";
    std::cout << "生产阶段耗时     : " << prod_ms << " ms\n";
    std::cout << "消费阶段耗时     : " << consume_ms << " ms\n";

    std::cout << "\n🚀 吞吐量\n";
    double total_s = total_ms / 1000.0;
    double throughput = submitted.load() / total_s;
    std::cout << "平均吞吐量       : " << throughput << " tasks/s\n";

    double peak_throughput = submitted.load() / (prod_ms / 1000.0);
    std::cout << "峰值吞吐量       : " << peak_throughput << " tasks/s\n";

    std::cout << "\n📦 批量效率\n";
    size_t batches = total_batches_popped.load();
    size_t tasks_popped = total_tasks_popped.load();
    double avg_batch_size = batches > 0 ? (double)tasks_popped / batches : 0;
    std::cout << "总共批次数       : " << batches << "\n";
    std::cout << "平均每批次任务数 : " << avg_batch_size << "\n";
    std::cout << "利用效率         : " << (avg_batch_size / BATCH_SIZE * 100) << "%\n";

    std::cout << "\n🧩 状态检查\n";
    std::cout << "调度器剩余任务   : " << peak_memory << " (应为 0)\n";
    if (peak_memory == 0) {
        std::cout << "✅ 所有任务已清空！\n";
    } else {
        std::cout << "⚠️ 仍有残留任务，请检查逻辑\n";
    }

    if (submitted == processed) {
        std::cout << "✅ 提交=处理 → 无丢失！\n";
    } else {
        std::cout << "❌ 任务不一致！差额: " << (submitted.load() - processed.load()) << "\n";
    }

    return 0;
}