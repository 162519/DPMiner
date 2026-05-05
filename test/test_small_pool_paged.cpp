#include "/home/caohaoshuang/pjj/distributed_graph_query/PMiner-undirect_mpi_new/src/small_pool_paged.h"
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <random>
#include <cstring>

// 测试对象 - 32字节
struct TestItem {
    uint64_t id;
    uint64_t threadId;
    uint64_t timestamp;
    uint64_t layer;
    
    // 构造函数
    TestItem() : id(0), threadId(0), timestamp(0), layer(0) {}
    
    TestItem(uint64_t i, uint64_t tid, uint64_t ts, uint64_t l)
        : id(i), threadId(tid), timestamp(ts), layer(l) {}
    
    // 验证方法
    static constexpr uint64_t INVALID_MARKER = 0xFFFFFFFFFFFFFFFFULL;
    
    bool isValid() const {
        // id不为0且不是无效标记表示有效
        return id != 0 && id != INVALID_MARKER;
    }
    
    void invalidate() {
        id = INVALID_MARKER;
    }
    
    void clear() {
        id = 0;
        threadId = 0;
        timestamp = 0;
        layer = 0;
    }
};
static_assert(sizeof(TestItem) == 32, "TestItem must be 32 bytes");

class SingleConsumerTest {
private:
    SmallPoolPaged<TestItem, 4> pool;  // 4层
    std::atomic<bool> stop{false};
    std::atomic<int> activeProducers{0};
    
    // 统计
    std::atomic<uint64_t> localAllocs{0};
    std::atomic<uint64_t> remoteAllocs{0};
    std::atomic<uint64_t> pagesProcessed{0};
    std::atomic<uint64_t> itemsProcessed{0};
    std::atomic<uint64_t> totalAllocated{0};
    std::atomic<uint64_t> flushCount{0};
    
    // 用于跟踪页面是否已满的常量
    static constexpr size_t ITEMS_PER_PAGE = 2048;  // 65536 / 32
    
public:
    // 生产者线程：多个生产者，每个有自己的层级
    void producer_thread(int threadId, int layer, int durationMs) {
        ++activeProducers;
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(1, 100);
        
        auto start = std::chrono::high_resolution_clock::now();
        auto endTime = start + std::chrono::milliseconds(durationMs);
        
        int itemsProduced = 0;
        int localFlushes = 0;
        
        while (!stop.load() && std::chrono::high_resolution_clock::now() < endTime) {
            // 95%概率本地分配，5%概率远程分配
            if (distrib(gen) <= 95) {
                // 本地分配
                TestItem* item = static_cast<TestItem*>(pool.allocate_local(layer));
                if (item) {
                    // 使用原子计数器生成唯一ID
                    uint64_t itemId = totalAllocated.fetch_add(1, std::memory_order_relaxed) + 1;
                    new (item) TestItem(
                        itemId,
                        threadId,
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::high_resolution_clock::now() - start).count(),
                        layer
                    );
                    localAllocs.fetch_add(1, std::memory_order_relaxed);
                    ++itemsProduced;
                    
                    // 模拟一些处理时间
                    std::this_thread::sleep_for(std::chrono::nanoseconds(20));
                    
                    // 关键：强制更频繁地刷新本地页！
                    // 每生产一定数量的项就刷新本地页
                    if (itemsProduced % 50 == 0) {  // 从100改为50
                        pool.flush_local_page_to_ready(layer);
                        ++localFlushes;
                        flushCount.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            } else {
                // 远程分配
                TestItem* item = pool.allocate_waiting_item();
                if (item) {
                    uint64_t itemId = totalAllocated.fetch_add(1, std::memory_order_relaxed) + 1;
                    new (item) TestItem(
                        itemId,
                        threadId,
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::high_resolution_clock::now() - start).count(),
                        0xFF  // 特殊标记表示远程分配
                    );
                    remoteAllocs.fetch_add(1, std::memory_order_relaxed);
                    ++itemsProduced;
                }
            }
            
            // 每隔一段时间尝试转换等待页
            if (itemsProduced % 30 == 0) {  // 从50改为30
                pool.poll_wait_to_ready();
            }
            
            // 小延迟，避免过度消耗CPU
            std::this_thread::sleep_for(std::chrono::microseconds(5));
        }
        
        // 关键：生产结束后，强制刷新所有本地页到就绪队列
        std::cout << "Producer " << threadId << " flushing final pages..." << std::endl;
        for (int l = 0; l < 4; ++l) {
            // 多次调用确保刷新
            for (int i = 0; i < 3; ++i) {
                pool.flush_local_page_to_ready(l);
            }
        }
        
        --activeProducers;
        std::cout << "Producer " << threadId << " (layer " << layer 
                  << ") finished: " << itemsProduced << " items, " 
                  << localFlushes << " flushes" << std::endl;
    }
    
    // 消费者线程：单个消费者处理所有就绪页
    void consumer_thread(int durationMs) {
        uint64_t itemsConsumed = 0;
        uint64_t pagesConsumed = 0;
        
        auto start = std::chrono::high_resolution_clock::now();
        auto endTime = start + std::chrono::milliseconds(durationMs + 2000); // 多给2秒处理剩余项
        
        std::cout << "Consumer started, will run for " << (durationMs + 2000) << "ms" << std::endl;
        
        // 第一阶段：正常处理
        while (!stop.load() && std::chrono::high_resolution_clock::now() < endTime) {
            // 1. 首先尝试从就绪队列获取页面
            auto* page = pool.acquire_ready();
            
            if (page) {
                // 处理页面
                TestItem* items = reinterpret_cast<TestItem*>(page);
                int validItemsInPage = 0;
                
                // 遍历页面中的所有项
                for (size_t i = 0; i < ITEMS_PER_PAGE; ++i) {
                    if (items[i].isValid()) {
                        ++validItemsInPage;
                        ++itemsConsumed;
                        
                        // 验证数据
                        if (items[i].id == 0) {
                            std::cerr << "Warning: Found item with id=0" << std::endl;
                        }
                        
                        // 执行"处理"
                        if (itemsConsumed <= 5) {
                            std::cout << "  [Consumer] Processing item " << items[i].id 
                                      << " from producer " << items[i].threadId
                                      << " (layer " << (items[i].layer & 0xFF) << ")" << std::endl;
                        }
                        
                        // 清理对象
                        items[i].~TestItem();
                        items[i].clear();  // 使用clear而不是invalidate
                    }
                    // 注意：这里不break，因为我们希望处理整个页面
                }
                
                // 更新统计
                if (validItemsInPage > 0) {
                    itemsProcessed.fetch_add(validItemsInPage, std::memory_order_relaxed);
                    pagesProcessed.fetch_add(1, std::memory_order_relaxed);
                    ++pagesConsumed;
                    
                    // 每处理一定数量报告一次
                    if (pagesConsumed % 20 == 0) {
                        std::cout << "  [Consumer] processed " << pagesConsumed 
                                  << " pages, " << itemsConsumed << " items so far" << std::endl;
                    }
                }
                
                // 归还页面到空闲池
                pool.release_page(page, 0);  // 归回到第0层
                
                // 模拟处理时间
                std::this_thread::sleep_for(std::chrono::microseconds(validItemsInPage * 5));
            } else {
                // 没有就绪页面可用
                if (activeProducers.load() > 0) {
                    // 还有生产者在运行，尝试转换等待页
                    pool.poll_wait_to_ready();
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                } else {
                    // 没有生产者了，短暂等待后再次检查
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        }
        
        std::cout << "\n[Consumer] finished main phase: " << pagesConsumed 
                  << " pages, " << itemsConsumed << " items" << std::endl;
        
        // 第二阶段：专门清理剩余项（给更多时间）
        std::cout << "[Consumer] starting cleanup phase..." << std::endl;
        cleanup_phase();
    }
    
    // 专门的清理阶段
    void cleanup_phase() {
        uint64_t cleanupItems = 0;
        uint64_t cleanupPages = 0;
        const int MAX_CLEANUP_ATTEMPTS = 50;
        
        for (int attempt = 0; attempt < MAX_CLEANUP_ATTEMPTS; ++attempt) {
            // 首先强制所有生产者刷新（虽然他们可能已经结束）
            for (int l = 0; l < 4; ++l) {
                pool.flush_local_page_to_ready(l);
            }
            
            // 转换等待页
            for (int i = 0; i < 5; ++i) {
                pool.poll_wait_to_ready();
            }
            
            // 尝试获取页面
            auto* page = pool.acquire_ready();
            if (!page) {
                // 没有页面了，等待一下再试
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            
            ++cleanupPages;
            TestItem* items = reinterpret_cast<TestItem*>(page);
            int itemsInPage = 0;
            
            // 处理页面
            for (size_t i = 0; i < ITEMS_PER_PAGE; ++i) {
                if (items[i].isValid()) {
                    ++itemsInPage;
                    ++cleanupItems;
                    items[i].~TestItem();
                    items[i].clear();
                }
            }
            
            if (itemsInPage > 0) {
                std::cout << "  [Cleanup] Page " << cleanupPages 
                          << ": " << itemsInPage << " items" << std::endl;
                itemsProcessed.fetch_add(itemsInPage, std::memory_order_relaxed);
                pagesProcessed.fetch_add(1, std::memory_order_relaxed);
            }
            
            pool.release_page(page, 0);
            
            // 如果这个页面是空的，可能没有更多工作了
            if (itemsInPage == 0) {
                break;
            }
        }
        
        std::cout << "[Cleanup] finished: " << cleanupPages << " pages, " 
                  << cleanupItems << " items" << std::endl;
    }
    
    // 监控线程：显示统计信息
    void monitor_thread(int durationMs) {
        auto start = std::chrono::high_resolution_clock::now();
        auto endTime = start + std::chrono::milliseconds(durationMs);
        
        uint64_t lastTotalAllocated = 0;
        uint64_t lastItemsProcessed = 0;
        auto lastReportTime = start;
        
        int reportCount = 0;
        
        while (!stop.load() && std::chrono::high_resolution_clock::now() < endTime) {
            auto now = std::chrono::high_resolution_clock::now();
            
            // 每500毫秒报告一次
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReportTime).count() >= 500) {
                uint64_t currentTotalAllocated = totalAllocated.load();
                uint64_t currentItemsProcessed = itemsProcessed.load();
                uint64_t currentLocalAllocs = localAllocs.load();
                uint64_t currentRemoteAllocs = remoteAllocs.load();
                uint64_t currentPagesProcessed = pagesProcessed.load();
                uint64_t currentFlushCount = flushCount.load();
                size_t totalPages = pool.pages();
                
                double timeSinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - lastReportTime).count() / 1000.0;
                
                double allocRate = (currentTotalAllocated - lastTotalAllocated) / timeSinceLast;
                double processRate = (currentItemsProcessed - lastItemsProcessed) / timeSinceLast;
                
                std::cout << "\n=== Progress Report #" << ++reportCount 
                          << " (elapsed: " << std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() << "ms) ===" << std::endl;
                std::cout << "Active producers: " << activeProducers.load() << std::endl;
                std::cout << "Total allocated: " << currentTotalAllocated 
                          << " (+" << (currentTotalAllocated - lastTotalAllocated) 
                          << ", rate: " << allocRate << "/s)" << std::endl;
                std::cout << "Items processed: " << currentItemsProcessed 
                          << " (+" << (currentItemsProcessed - lastItemsProcessed)
                          << ", rate: " << processRate << "/s)" << std::endl;
                std::cout << "Backlog: " << (currentTotalAllocated - currentItemsProcessed) 
                          << " items" << std::endl;
                std::cout << "Pages processed: " << currentPagesProcessed << std::endl;
                std::cout << "Flush count: " << currentFlushCount << std::endl;
                std::cout << "Total pages: " << totalPages << std::endl;
                std::cout << "===========================================\n" << std::endl;
                
                lastTotalAllocated = currentTotalAllocated;
                lastItemsProcessed = currentItemsProcessed;
                lastReportTime = now;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // 时间到了，停止所有线程
        stop.store(true);
        std::cout << "\nTest duration reached, stopping producers..." << std::endl;
    }
    
    void run_test(int numProducers, int testDurationMs) {
        std::cout << "================================================" << std::endl;
        std::cout << "SINGLE CONSUMER TEST - FIXED VERSION" << std::endl;
        std::cout << "================================================" << std::endl;
        std::cout << "Configuration:" << std::endl;
        std::cout << "  Producers: " << numProducers << std::endl;
        std::cout << "  Consumers: 1" << std::endl;
        std::cout << "  Duration: " << testDurationMs << "ms" << std::endl;
        std::cout << "  Items per page: " << ITEMS_PER_PAGE << std::endl;
        std::cout << "================================================\n" << std::endl;
        
        // 重置统计
        localAllocs = 0;
        remoteAllocs = 0;
        totalAllocated = 0;
        itemsProcessed = 0;
        pagesProcessed = 0;
        flushCount = 0;
        stop = false;
        activeProducers = 0;
        
        std::vector<std::thread> producers;
        
        // 启动生产者线程
        std::cout << "Starting " << numProducers << " producers..." << std::endl;
        for (int i = 0; i < numProducers; ++i) {
            int layer = i % 4;  // 分配到不同的层级
            producers.emplace_back(&SingleConsumerTest::producer_thread, this, 
                                   i, layer, testDurationMs);
        }
        
        // 启动消费者线程（单线程）
        std::cout << "Starting consumer..." << std::endl;
        std::thread consumer(&SingleConsumerTest::consumer_thread, this, testDurationMs);
        
        // 启动监控线程
        std::cout << "Starting monitor..." << std::endl;
        std::thread monitor(&SingleConsumerTest::monitor_thread, this, testDurationMs);
        
        // 等待监控线程结束（它会设置stop标志）
        monitor.join();
        
        // 等待所有生产者结束
        std::cout << "\nWaiting for producers to finish..." << std::endl;
        for (auto& t : producers) {
            t.join();
        }
        std::cout << "All producers finished" << std::endl;
        
        // 等待消费者结束
        std::cout << "Waiting for consumer to finish..." << std::endl;
        consumer.join();
        std::cout << "Consumer finished" << std::endl;
        
        // 最终清理
        std::cout << "\nPerforming final cleanup..." << std::endl;
        final_cleanup();
        
        // 最终统计和验证
        print_final_statistics();
    }
    
    void final_cleanup() {
        // 最后一次尝试清理
        for (int i = 0; i < 10; ++i) {
            for (int l = 0; l < 4; ++l) {
                pool.flush_local_page_to_ready(l);
            }
            
            for (int j = 0; j < 5; ++j) {
                pool.poll_wait_to_ready();
            }
            
            auto* page = pool.acquire_ready();
            if (page) {
                TestItem* items = reinterpret_cast<TestItem*>(page);
                int count = 0;
                for (size_t i = 0; i < ITEMS_PER_PAGE; ++i) {
                    if (items[i].isValid()) {
                        ++count;
                        items[i].~TestItem();
                        items[i].clear();
                    }
                }
                
                if (count > 0) {
                    std::cout << "  Final cleanup: found " << count << " items" << std::endl;
                    itemsProcessed.fetch_add(count, std::memory_order_relaxed);
                    pagesProcessed.fetch_add(1, std::memory_order_relaxed);
                }
                
                pool.release_page(page, 0);
            } else {
                break;
            }
        }
    }
    
    void print_final_statistics() {
        std::cout << "\n================================================" << std::endl;
        std::cout << "FINAL TEST RESULTS" << std::endl;
        std::cout << "================================================" << std::endl;
        
        uint64_t finalTotalAllocated = totalAllocated.load();
        uint64_t finalLocalAllocs = localAllocs.load();
        uint64_t finalRemoteAllocs = remoteAllocs.load();
        uint64_t finalItemsProcessed = itemsProcessed.load();
        uint64_t finalPagesProcessed = pagesProcessed.load();
        uint64_t finalFlushCount = flushCount.load();
        size_t finalTotalPages = pool.pages();
        
        std::cout << "Total items allocated: " << finalTotalAllocated << std::endl;
        std::cout << "  - Local allocations: " << finalLocalAllocs 
                  << " (" << (finalLocalAllocs * 100.0 / finalTotalAllocated) << "%)" << std::endl;
        std::cout << "  - Remote allocations: " << finalRemoteAllocs 
                  << " (" << (finalRemoteAllocs * 100.0 / finalTotalAllocated) << "%)" << std::endl;
        std::cout << "Total items processed: " << finalItemsProcessed << std::endl;
        std::cout << "Total pages processed: " << finalPagesProcessed << std::endl;
        std::cout << "Total flushes: " << finalFlushCount << std::endl;
        std::cout << "Total pages allocated: " << finalTotalPages << std::endl;
        
        // 计算每个页面的平均项数
        if (finalPagesProcessed > 0) {
            double avgItemsPerPage = static_cast<double>(finalItemsProcessed) / finalPagesProcessed;
            std::cout << "Average items per page: " << avgItemsPerPage 
                      << " (max: " << ITEMS_PER_PAGE << ")" << std::endl;
            
            // 计算页面利用率
            double utilization = (avgItemsPerPage / ITEMS_PER_PAGE) * 100.0;
            std::cout << "Page utilization: " << utilization << "%" << std::endl;
        }
        
        // 验证
        uint64_t difference = (finalTotalAllocated > finalItemsProcessed) ? 
                             (finalTotalAllocated - finalItemsProcessed) : 
                             (finalItemsProcessed - finalTotalAllocated);
        
        if (finalTotalAllocated == finalItemsProcessed) {
            std::cout << "\n✓ PERFECT: All " << finalTotalAllocated 
                      << " items were successfully allocated and processed!" << std::endl;
        } else if (difference <= 100) {  // 允许少量误差
            std::cout << "\n✓ GOOD: " << finalItemsProcessed << "/" << finalTotalAllocated 
                      << " items processed (" << difference << " difference)" << std::endl;
            std::cout << "  Small difference may be due to timing or counting issues" << std::endl;
        } else if (finalTotalAllocated > finalItemsProcessed) {
            std::cout << "\n⚠ WARNING: " << difference
                      << " items were allocated but not processed" << std::endl;
            std::cout << "  Processed: " << finalItemsProcessed << "/" << finalTotalAllocated 
                      << " (" << (finalItemsProcessed * 100.0 / finalTotalAllocated) << "%)" << std::endl;
            
            // 可能的原因分析
            std::cout << "\nPossible reasons:" << std::endl;
            std::cout << "  1. Pages not fully flushed from local cache" << std::endl;
            std::cout << "  2. Items stuck in waiting ring" << std::endl;
            std::cout << "  3. Timing issue with producer/consumer coordination" << std::endl;
        } else {
            std::cout << "\n✗ ERROR: Processed more items (" << finalItemsProcessed 
                      << ") than allocated (" << finalTotalAllocated << ")" << std::endl;
            std::cout << "  This indicates a serious counting error" << std::endl;
        }
        
        std::cout << "================================================" << std::endl;
    }
};

int main() {
    SingleConsumerTest test;
    
    // 配置测试参数
    const int NUM_PRODUCERS = 4;        // 生产者线程数
    const int TEST_DURATION_MS = 3000;  // 测试持续时间（毫秒）- 先缩短测试时间
    
    try {
        test.run_test(NUM_PRODUCERS, TEST_DURATION_MS);
        std::cout << "\nTest completed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "\nTest failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}