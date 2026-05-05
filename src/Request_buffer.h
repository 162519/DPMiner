// #include <tbb/concurrent_unordered_set.h>
// #include <vector>
// #include <thread>
// #include <mutex>
// #include <condition_variable>
// #include <chrono>
// #include "../util/global.h"
// #include <iostream>

// class RequestBuffer {
// private:
//     std::mutex batchMutex;                              // 批量请求锁
//     std::condition_variable cv;                         // 条件变量
//     int batchSize;                                 // 当前批量大小
//     int maxBatchSize;                              // 最大批量大小
//     int minBatchSize;                              // 最小批量大小
//     std::chrono::milliseconds timeout;                  // 超时时间
//     std::atomic<bool> stopFlag{false};
//     std::thread workerThread;                           // 工作线程
//     //tbb::concurrent_vector<unsigned> requestBuffer;
//     tbb::concurrent_vector<unsigned> requestBuffer;
//     std::atomic<unsigned> bufferSize{0};

//     // // 模拟发送请求的函数
//     // void sendRequests(const std::vector<unsigned>& batch) {
//     //     std::cout << "Sending batch of size: " << batch.size() << std::endl;
//     //     // 模拟发送请求的逻辑
//     //     std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 模拟网络延迟
//     // }

//     // 动态调整批量大小
//     void adjustBatchSize(std::chrono::milliseconds responseTime) {
//         if (responseTime > timeout) {
//             batchSize = std::max(minBatchSize, batchSize / 2); // 减小批量大小
//         } else {
//             batchSize = std::min(maxBatchSize, batchSize + 1); // 增大批量大小
//         }
//         std::cout << "Adjusted batch size to: " << batchSize << std::endl;
//     }

//     // // 工作线程函数
//     // void processRequests() {
//     //     while (!stopFlag) {
//     //         std::vector<unsigned> batch;
//     //         auto startTime = std::chrono::steady_clock::now();

//     //         {
//     //             std::unique_lock<std::mutex> lock(batchMutex);
//     //             if (cv.wait_for(lock, timeout, [this] { return !requestSet.empty(); })) {
//     //                 // 将当前的请求集合转为批量请求
//     //                 batch.assign(requestSet.begin(), requestSet.end());
//     //                 requestSet.clear();
//     //                 requestCount.store(0, std::memory_order_release);  // 原子重置
//     //             }
//     //         }

//     //         if (!batch.empty()) {
//     //             auto endTime = std::chrono::steady_clock::now();
//     //             auto responseTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

//     //             // // 发送请求
//     //             // sendRequests(batch);
//     //             getRemoteData(batch);
//     //             request_num += batch.size();
//     //             // 动态调整批量大小
//     //             adjustBatchSize(responseTime);
//     //         }
//     //     }
//     // }

//     // void processRequests() {
//     //     while (!stopFlag.load(std::memory_order_acquire)) {
//     //         std::vector<unsigned> batch;
//     //         auto startTime = std::chrono::steady_clock::now();
            
//     //         {
//     //             std::unique_lock<std::mutex> lock(batchMutex);
                
//     //             // 使用 wait_for 实现超时机制
//     //             bool triggered = cv.wait_for(lock, timeout, [this] { 
//     //                 return bufferSize.load(std::memory_order_acquire) >= batchSize || 
//     //                        stopFlag.load(std::memory_order_acquire); 
//     //             });
                
//     //             if (stopFlag.load(std::memory_order_acquire)) break;
                
//     //             // 即使没有达到batchSize，超时后也处理当前所有请求
//     //             size_t currentSize = bufferSize.load(std::memory_order_acquire);
//     //             if (currentSize > 0) {
//     //                 batch.reserve(currentSize);
                    
//     //                 // tbb::concurrent_vector 可以安全遍历
//     //                 // 注意：这里使用索引访问更安全
//     //                 for (size_t i = 0; i < currentSize; i++) {
//     //                     batch.push_back(requestBuffer[i]);
//     //                 }
                    
//     //                 // 清空缓冲区
//     //                 requestBuffer.clear();
//     //                 dedupSet.clear();
//     //                 bufferSize.store(0, std::memory_order_release);
//     //             }
//     //         }
            
//     //         if (!batch.empty()) {
//     //             // 处理批次
//     //             getRemoteData(batch);
                
//     //             // 动态调整批量大小（基于处理时间）
//     //             auto endTime = std::chrono::steady_clock::now();
//     //             auto processTime = std::chrono::duration_cast<std::chrono::milliseconds>(
//     //                 endTime - startTime);
//     //             adjustBatchSize(processTime);
//     //         }
//     //     }
        
//     //     // // 处理剩余的请求
//     //     // flushRemaining();
//     // }
//     void processRequests() {
//         while (!stopFlag) {
//             std::vector<unsigned> batch;
//             auto startTime = std::chrono::steady_clock::now();
//             {
//                 std::unique_lock<std::mutex> batchLock(batchMutex);
//                 cv.wait_for(batchLock, timeout, [this] {
//                     return bufferSize.load() >= batchSize || stopFlag;
//                 });
                
//                 if (stopFlag) break;
                
//                                 // 获取当前所有顶点
//                 size_t currentSize = bufferSize.load();
//                 if (currentSize > 0) {
//                     batch.assign(std::make_move_iterator(requestBuffer.begin()),
//                     std::make_move_iterator(requestBuffer.end()));
//                     cout<<"realsize: "<<batch.size()<<endl;
//                     // 清空缓冲区
//                     requestBuffer.clear();
//                     bufferSize.store(0);
//                 }
//             }
            
//             if (!batch.empty()) {
//                 getRemoteData(batch);
                
//                 // 动态调整批量大小（基于处理时间）
//                 auto endTime = std::chrono::steady_clock::now();
//                 auto processTime = std::chrono::duration_cast<std::chrono::milliseconds>(
//                     endTime - startTime);
//                 adjustBatchSize(processTime);
//             }
//         }
//     }

// public:
//     RequestBuffer(int initialBatchSize, int maxBatch, int minBatch, std::chrono::milliseconds timeoutDuration)
//         : batchSize(initialBatchSize), maxBatchSize(maxBatch), minBatchSize(minBatch), timeout(timeoutDuration), stopFlag(false) {
//         workerThread = std::thread(&RequestBuffer::processRequests, this);
//     }

//     ~RequestBuffer() {
//         stopFlag = true;
//         cv.notify_all();
//         if (workerThread.joinable()) {
//             workerThread.join();
//         }
//     }

//     // 添加顶点到请求集合
//     // void addVertex(unsigned vertex) {
//     //     // 快速去重检查
//     //     if (dedupSet.find(vertex) != dedupSet.end()) {
//     //         return;
//     //     }
        
//     //     // 插入到缓冲区和去重集
//     //     requestBuffer.push_back(vertex);
//     //     dedupSet.insert(vertex);
        
//     //     unsigned newSize = bufferSize.fetch_add(1, std::memory_order_acq_rel) + 1;
        
//     //     if (newSize >= batchSize) {
//     //         std::unique_lock<std::mutex> lock(batchMutex);
//     //         cv.notify_one();
//     //     }
//     // }

//     void addVertex(unsigned vertex) {
//         requestBuffer.push_back(vertex);
//         bufferSize.fetch_add(1);
//         //unsigned newSize = bufferSize.fetch_add(1) + 1;
//         if (bufferSize >= batchSize) {
//             std::unique_lock<std::mutex> lock(batchMutex);
//             cv.notify_one();
//         }
//     }

//     void addVertices(const std::vector<unsigned>& vertices) {
//         for (unsigned v : vertices) {
//             requestBuffer.push_back(v);
//         }
//         bufferSize.fetch_add(vertices.size());
//         if (bufferSize >= batchSize) {
//             std::unique_lock<std::mutex> lock(batchMutex);
//             cv.notify_one();
//         }
//     }
// };


#include <tbb/concurrent_vector.h>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <iostream>
#include <memory>

class RequestBuffer {
private:
    // 双重缓冲区：一个用于收集，一个用于处理
    struct Buffer {
        tbb::concurrent_vector<unsigned> data;
        std::atomic<size_t> count{0};
        
        void clear() {
            data.clear();
            count.store(0, std::memory_order_release);
        }
        
        size_t size() const {
            return count.load(std::memory_order_acquire);
        }
        
        bool empty() const {
            return size() == 0;
        }
        
        void push_back(unsigned vertex) {
            data.push_back(vertex);
            count.fetch_add(1, std::memory_order_acq_rel);
        }
    };
    
    // 使用智能指针管理缓冲区，实现快速交换
    std::shared_ptr<Buffer> collectingBuffer_;
    std::shared_ptr<Buffer> processingBuffer_;
    
    std::mutex bufferMutex_;
    std::condition_variable cv_;
    std::atomic<bool> stopFlag_{false};
    std::thread workerThread_;
    
    // 配置参数
    std::atomic<int> batchSize_;
    const int maxBatchSize_;
    const int minBatchSize_;
    std::chrono::milliseconds timeout_;
    
    // 性能监控
    std::atomic<size_t> totalProcessed_{0};
    std::atomic<size_t> totalBatches_{0};

private:
    // 动态调整批量大小
    void adjustBatchSize(std::chrono::milliseconds processTime, size_t currentBatchSize) {
        int current = batchSize_.load(std::memory_order_relaxed);
        int newSize = current;
        
        if (processTime >= timeout_) {
            newSize = std::max(minBatchSize_, current / 2);
            timeout_ = std::chrono::milliseconds(static_cast<int64_t>(timeout_.count()*(currentBatchSize/double(current))));
        } else {
            newSize = std::min(maxBatchSize_, current + 1);

        }
        
        if (newSize != current) {
            batchSize_.store(newSize, std::memory_order_release);
            std::cout << "[BatchSize] Adjusted from " << current 
                      << " to " << newSize 
                      << " (process time: " << processTime.count() << "ms)" << std::endl;
        }
    }
    
    // 工作线程主循环
    void processRequests() {
        while (!stopFlag_.load(std::memory_order_acquire)) {
            std::shared_ptr<Buffer> bufferToProcess;
            auto startTime = std::chrono::steady_clock::now();
            
            {
                std::unique_lock<std::mutex> lock(bufferMutex_);
                
                // 等待条件：达到批次大小或超时
                bool hasEnoughData = cv_.wait_for(lock, timeout_, [this] {
                    return collectingBuffer_->size() >= static_cast<size_t>(batchSize_.load()) 
                           || stopFlag_.load();
                });
                
                if (stopFlag_.load()) break;
                
                // 即使数据不足，如果超时且有数据也处理
                if (hasEnoughData || !collectingBuffer_->empty()) {
                    // 快速交换缓冲区
                    std::swap(collectingBuffer_, processingBuffer_);
                    
                    // 重置收集缓冲区
                    collectingBuffer_->clear();
                    
                    bufferToProcess = processingBuffer_;
                }
            }
            
            // 处理数据（不在锁内）
            if (bufferToProcess && !bufferToProcess->empty()) {
                size_t batchSize = bufferToProcess->size();
                
                // 转换为vector进行高效处理
                std::vector<unsigned> batch;
                batch.reserve(batchSize);
                
                // 批量移动数据（最高效的方式）
                auto& source = bufferToProcess->data;
                batch.assign(std::make_move_iterator(source.begin()),
                            std::make_move_iterator(source.begin() + batchSize));
                cout<<"realsize: "<<batch.size()<<endl;
                // 执行远程数据获取
                getRemoteData(batch);
                
                // 更新统计
                totalProcessed_.fetch_add(batchSize, std::memory_order_relaxed);
                totalBatches_.fetch_add(1, std::memory_order_relaxed);
                
                // 动态调整批次大小
                auto endTime = std::chrono::steady_clock::now();
                auto processTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                    endTime - startTime);
                adjustBatchSize(processTime, batchSize);
                
                // 清空处理缓冲区
                bufferToProcess->clear();
                
                // 输出性能信息
                if (totalBatches_.load() % 10 == 0) {
                    std::cout << "[Performance] Total processed: " 
                              << totalProcessed_.load() 
                              << " in " << totalBatches_.load() 
                              << " batches" << std::endl;
                }
            }
        }
        
        // 处理剩余数据
        flushRemaining();
    }
    
    // 刷新剩余数据
    void flushRemaining() {
        if (!collectingBuffer_->empty()) {
            std::vector<unsigned> remaining;
            remaining.reserve(collectingBuffer_->size());
            
            auto& source = collectingBuffer_->data;
            remaining.assign(std::make_move_iterator(source.begin()),
                           std::make_move_iterator(source.end()));
            
            if (!remaining.empty()) {
                std::cout << "[Flush] Processing remaining " 
                          << remaining.size() << " vertices" << std::endl;
                getRemoteData(remaining);
                totalProcessed_.fetch_add(remaining.size());
            }
        }
    }

public:
    RequestBuffer(int initialBatchSize, int maxBatch, int minBatch, 
                  std::chrono::milliseconds timeoutDuration)
        : batchSize_(initialBatchSize), 
          maxBatchSize_(maxBatch), 
          minBatchSize_(minBatch), 
          timeout_(timeoutDuration) {
        
        // 初始化双重缓冲区
        collectingBuffer_ = std::make_shared<Buffer>();
        processingBuffer_ = std::make_shared<Buffer>();
        
        // 启动工作线程
        workerThread_ = std::thread(&RequestBuffer::processRequests, this);
        
        std::cout << "[RequestBuffer] Initialized with batch size: " 
                  << initialBatchSize 
                  << ", timeout: " << timeoutDuration.count() << "ms" 
                  << std::endl;
    }
    
    ~RequestBuffer() {
        // 停止工作线程
        stopFlag_.store(true, std::memory_order_release);
        cv_.notify_all();
        
        if (workerThread_.joinable()) {
            workerThread_.join();
        }
        
        std::cout << "[RequestBuffer] Destroyed. Total processed: " 
                  << totalProcessed_.load() 
                  << " vertices in " << totalBatches_.load() 
                  << " batches" << std::endl;
    }
    
    // 添加单个顶点（高性能无锁版本）
    void addVertex(unsigned vertex) {
        collectingBuffer_->push_back(vertex);
        
        // 检查是否需要通知（使用无锁检查）
        if (collectingBuffer_->size() >= static_cast<size_t>(batchSize_.load(std::memory_order_acquire))) {
            std::lock_guard<std::mutex> lock(bufferMutex_);
            cv_.notify_one();
        }
    }
    
    // 批量添加顶点
    void addVertices(const std::vector<unsigned>& vertices) {
        if (vertices.empty()) return;
        
        auto& buffer = collectingBuffer_;
        
        // 批量添加
        for (unsigned v : vertices) {
            buffer->data.push_back(v);
        }
        
        // 原子更新计数
        size_t added = vertices.size();
        buffer->count.fetch_add(added, std::memory_order_acq_rel);
        
        // 检查是否需要通知
        if (buffer->size() >= static_cast<size_t>(batchSize_.load(std::memory_order_acquire))) {
            std::lock_guard<std::mutex> lock(bufferMutex_);
            cv_.notify_one();
        }
    }
    
    // 立即处理当前缓冲区（手动刷新）
    void flush() {
        std::unique_lock<std::mutex> lock(bufferMutex_);
        
        if (!collectingBuffer_->empty()) {
            std::swap(collectingBuffer_, processingBuffer_);
            collectingBuffer_->clear();
            
            lock.unlock();
            
            // 处理数据
            if (!processingBuffer_->empty()) {
                std::vector<unsigned> batch;
                batch.reserve(processingBuffer_->size());
                
                auto& source = processingBuffer_->data;
                batch.assign(std::make_move_iterator(source.begin()),
                           std::make_move_iterator(source.begin() + processingBuffer_->size()));
                
                getRemoteData(batch);
                processingBuffer_->clear();
            }
        }
    }
    
    // 获取当前状态
    size_t getCurrentBufferSize() const {
        return collectingBuffer_->size();
    }
    
    size_t getTotalProcessed() const {
        return totalProcessed_.load();
    }
    
    size_t getTotalBatches() const {
        return totalBatches_.load();
    }
    
    int getCurrentBatchSize() const {
        return batchSize_.load();
    }
    
    // 禁止拷贝
    RequestBuffer(const RequestBuffer&) = delete;
    RequestBuffer& operator=(const RequestBuffer&) = delete;
    
    // 允许移动
    RequestBuffer(RequestBuffer&&) = default;
    RequestBuffer& operator=(RequestBuffer&&) = default;
};
