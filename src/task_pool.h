#pragma once

#include <atomic>
#include <vector>
#include <memory>
#include <mutex>
#include <cstddef>
#include <cstring>
#include <thread>
#include <iostream>
#include <stdexcept>

template <typename T>
class SmallPool {
    static_assert(sizeof(T) == 32, "只支持 32 B 结构体");
    static_assert(32 % alignof(T) == 0 || alignof(T) <= 32,
                  "T 的对齐要求不能超过块大小");
    //static_assert(kBatch <= kBlocksPerPage, "kBatch 不能大于每页块数");

private:
    static constexpr size_t kBlockSize = 32;
    static constexpr size_t kBatch = 64;             // 每次拿 64 块
    static constexpr size_t kPageSize = 65536;       // 64 KB 页
    static constexpr size_t kBlocksPerPage = kPageSize / kBlockSize; // 2048

    struct alignas(kBlockSize) Block { 
        char data[kBlockSize]; 
    };
    
 
    struct Page {
        Block blocks[kBlocksPerPage];           // 64KB
        std::atomic<uint32_t> freeCount{kBlocksPerPage};
        uint32_t freeList[kBlocksPerPage];      // 空闲序号栈
        
        Page() {
            // 初始化空闲列表：0 ~ 2047
            for (uint32_t i = 0; i < kBlocksPerPage; ++i) {
                freeList[i] = i;
            }
        }
        
        //尝试从本页分配一批
        bool try_allocate_batch(uint32_t batch_size, uint32_t page_base, 
                               uint32_t* output) {
            uint32_t current = freeCount.load(std::memory_order_relaxed);
            
            while (true) {
                if (current < batch_size) {
                    return false;  // 空间不足
                }
                
                if (freeCount.compare_exchange_weak(current, current - batch_size,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed)) {
                    // 成功获取批次
                    for (uint32_t i = 0; i < batch_size; ++i) {
                        output[i] = page_base + freeList[current - 1 - i];
                    }
                    return true;
                }
                // CAS失败，重试
            }
        }
    };

    std::vector<std::unique_ptr<Page>> pages_;
    mutable std::mutex grow_mutex_;  // 保护 pages_ 的增长
    
    // 全局空闲池
    struct GlobalFreePool {
        static constexpr size_t kCapacity = 65536;  // 64K个槽位
        
        std::unique_ptr<std::atomic<uint32_t>[]> slots;
        std::atomic<uint32_t> top{0};
        
        GlobalFreePool() : slots(std::make_unique<std::atomic<uint32_t>[]>(kCapacity)) {}
        
        bool push(uint32_t idx) {
            uint32_t current_top = top.load(std::memory_order_relaxed);
            if (current_top >= kCapacity) {
                return false;
            }
            slots[current_top].store(idx, std::memory_order_relaxed);
            top.store(current_top + 1, std::memory_order_release);
            return true;
        }
        
        bool pop_batch(uint32_t batch_size, uint32_t* output) {
            uint32_t current_top = top.load(std::memory_order_acquire);
            if (current_top < batch_size) {
                return false;
            }
            // 用 fetch_sub 原子扣减，防止负数
            uint32_t new_top = top.fetch_sub(batch_size, std::memory_order_acq_rel);
            if (new_top < batch_size) [[unlikely]] {
                // 回滚并失败
                top.fetch_add(batch_size, std::memory_order_release);
                return false;
            }
            for (uint32_t i = 0; i < batch_size; ++i) {
                output[i] = slots[new_top - batch_size + i].load(std::memory_order_relaxed);
            }
            return true;
        }
        
        size_t size() const {
            return top.load(std::memory_order_relaxed);
        }
    };
    
    mutable GlobalFreePool global_pool_;

    // 线程局部缓存
    struct ThreadLocalCache {
        uint32_t indices[kBatch];
        uint32_t count = 0;
        uint32_t recycled = 0;  // 本地回收计数
        
        bool empty() const { return count == 0; }
        
        void push(uint32_t idx) {
            if (count < kBatch) {
                indices[count++] = idx;
            }
        }
        
        uint32_t pop() {
            return indices[--count];
        }
        
        void clear() {
            count = 0;
            recycled = 0;
        }
    };
    
    static thread_local ThreadLocalCache tls_cache_;

    // 【关键修改】内部无锁扩容函数，必须由持有 grow_mutex_ 的上下文调用
    Page* do_grow() {
        auto new_page = std::make_unique<Page>();
        Page* page_ptr = new_page.get();
        pages_.push_back(std::move(new_page));
        page_ptr->freeCount.store(kBlocksPerPage, std::memory_order_release);
        std::cout << "[SmallPool] 扩展新页#" << pages_.size() 
                  << ", 总块数: " << pages_.size() * kBlocksPerPage << std::endl;
        return page_ptr;
    }

    bool refill_local_cache() {
        ThreadLocalCache& cache = tls_cache_;

        // Step 1: 尝试从全局池获取一批
        if (global_pool_.pop_batch(kBatch, cache.indices)) {
            cache.count = kBatch;
            return true;
        }

        // Step 2: 锁定 grow_mutex_，安全访问 pages_
        std::lock_guard<std::mutex> lock(grow_mutex_);

        // 若没有任何页，则创建第一页
        if (pages_.empty()) {
            do_grow();
        }

        Page* latest_page = pages_.back().get();
        uint32_t page_base = static_cast<uint32_t>((pages_.size() - 1) * kBlocksPerPage);

        // 尝试从最新页分配
        if (latest_page->try_allocate_batch(kBatch, page_base, cache.indices)) {
            cache.count = kBatch;
            return true;
        }

        // // 当前页空间不足 → 扩容新页
        // std::cerr << "[T" << std::this_thread::get_id() 
        //           << "] 当前页空间不足，扩展新页\n";

        Page* new_page = do_grow();
        page_base = static_cast<uint32_t>((pages_.size() - 1) * kBlocksPerPage);

        // 新页刚创建，必然足够分配
        bool success = new_page->try_allocate_batch(kBatch, page_base, cache.indices);
        if (success) {
            cache.count = kBatch;
            return true;
        }

        // 理论上不会失败，除非 kBatch > kBlocksPerPage
        // std::cerr << "[T" << std::this_thread::get_id() 
        //           << "] 新页分配失败！可能是 batch 过大或逻辑错误\n";
        return false;
    }

public:
    SmallPool() {
        std::cout << "[SmallPool] 初始化，块大小: " << sizeof(T) 
                  << "B, 每页块数: " << kBlocksPerPage << std::endl;

        std::lock_guard<std::mutex> lock(grow_mutex_);
        if (pages_.empty()) {
            do_grow();
        }
    }
    
    /* 移动构造/赋值 */
    SmallPool(SmallPool&&) noexcept = default;
    SmallPool& operator=(SmallPool&&) noexcept = default;

    /* 明确禁止拷贝 */
    SmallPool(const SmallPool&)            = delete;
    SmallPool& operator=(const SmallPool&) = delete;
    ~SmallPool() {
        std::cout << "[SmallPool] 析构，总页数: " << pages_.size() 
                  << ", 总块数: " << total_blocks() 
                  << ", 全局空闲: " << free_blocks() << std::endl;
    }

    uint32_t allocate() {
        ThreadLocalCache& cache = tls_cache_;
        if (cache.empty()) {
            if (!refill_local_cache()) {
                std::cerr << "[T" << std::this_thread::get_id() 
                          << "] refill 失败，返回 -1\n";
                return uint32_t(-1);
            }
            //refill_local_cache();
        }
        return cache.pop();
    }

    void free(uint32_t idx) {
        if (idx == uint32_t(-1)) return;
        
        ThreadLocalCache& cache = tls_cache_;
        
        // 优先放入本地缓存
        if (cache.count < kBatch) {
            cache.push(idx);
            cache.recycled++;
            
            // 如果本地回收达到 batch 数量，批量提交到全局池
            if (cache.recycled >= kBatch) {
                flush_local_cache();
            }
        } else {
            // 本地缓存满，直接提交
            if (!global_pool_.push(idx)) {
                //std::cerr << "[SmallPool] 全局池满，丢弃块 #" << idx << std::endl;
            }
        }
    }
    void flush_local_cache() {
        ThreadLocalCache& cache = tls_cache_;
        if (cache.count == 0) return;

        // 批量提交到全局池
        for (uint32_t i = 0; i < cache.count; ++i) {
            global_pool_.push(cache.indices[i]);
        }
        cache.clear();
    }

    T& at(uint32_t idx) {
        if (idx == uint32_t(-1)) {
            throw std::runtime_error("无效索引: -1");
        }
        
        uint32_t page_idx = idx / kBlocksPerPage;
        uint32_t block_idx = idx % kBlocksPerPage;
        
        std::lock_guard<std::mutex> lock(grow_mutex_); // 安全访问 pages_
        if (page_idx >= pages_.size()) {
            throw std::out_of_range("索引超出范围: page_idx=" + std::to_string(page_idx) +
                                    ", pages.size()=" + std::to_string(pages_.size()));
        }
        
        return *reinterpret_cast<T*>(pages_[page_idx]->blocks[block_idx].data);
    }

    const T& at(uint32_t idx) const {
        if (idx == uint32_t(-1)) {
            throw std::runtime_error("无效索引: -1");
        }
        
        uint32_t page_idx = idx / kBlocksPerPage;
        uint32_t block_idx = idx % kBlocksPerPage;
        
        std::lock_guard<std::mutex> lock(grow_mutex_); // 安全访问 pages_
        if (page_idx >= pages_.size()) {
            throw std::out_of_range("索引超出范围: page_idx=" + std::to_string(page_idx) +
                                    ", pages.size()=" + std::to_string(pages_.size()));
        }
        
        return *reinterpret_cast<const T*>(pages_[page_idx]->blocks[block_idx].data);
    }

    size_t total_blocks() const { 
        std::lock_guard<std::mutex> lock(grow_mutex_);
        return pages_.size() * kBlocksPerPage; 
    }
    
    size_t free_blocks() const { 
        return global_pool_.size(); 
    }
    
    size_t page_count() const { 
        std::lock_guard<std::mutex> lock(grow_mutex_);
        return pages_.size(); 
    }
    
    size_t blocks_per_page() const {
        return kBlocksPerPage;
    }
    
    double memory_efficiency() const {
        size_t total = total_blocks();
        if (total == 0) return 0.0;
        size_t used = total - free_blocks();
        return static_cast<double>(used) / total;
    }
};

// 定义线程局部变量
template <typename T>
thread_local typename SmallPool<T>::ThreadLocalCache SmallPool<T>::tls_cache_{};