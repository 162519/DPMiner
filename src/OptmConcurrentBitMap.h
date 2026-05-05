
// #ifndef CONCURRENTBITMAP_H
// #define CONCURRENTBITMAP_H

// #include <atomic>
// #include <vector>
// #include <cstdint>
// #include <fstream>
// #include <sstream>
// #include <iostream>
// #include <bitset>
// #include <tbb/concurrent_vector.h>
// using namespace std;
// using namespace tbb;
// typedef unsigned R_ID;

// #define REMOTE   0
// #define LOCAL    1
// #define INTEGRAL 3

// class concurrentBitMap {
// private:
//     int  worker_num_;
//     R_ID num_v_;
//     int  my_rank_;

//     /* 每字节 8 位，最多 8 台机器 */
//     tbb::concurrent_vector<std::atomic<std::uint8_t>> vertIndex_;
//     tbb::concurrent_vector<std::atomic<std::uint8_t>> vertNeiFullNum_;

//     static bool atomic_test(const std::atomic<std::uint8_t>& byte, int bit) {
//         std::uint8_t mask = 1u << bit;
//         return (byte.load(std::memory_order_relaxed) & mask) != 0;
//     }
//     static void atomic_set(std::atomic<std::uint8_t>& byte, int bit) {
//         std::uint8_t mask = 1u << bit;
//         std::uint8_t old = byte.load(std::memory_order_relaxed);
//         while (!byte.compare_exchange_weak(old, old | mask,
//                                            std::memory_order_relaxed)) {}
//     }

// public:
//     concurrentBitMap(int wk, R_ID nv, int rank)
//         : worker_num_(wk), num_v_(nv), my_rank_(rank) {
//         vertIndex_.grow_to_at_least(num_v_);
//         vertNeiFullNum_.grow_to_at_least(num_v_);
//         for (R_ID i = 0; i < num_v_; ++i) {
//             vertIndex_[i].store(0, std::memory_order_relaxed);
//             vertNeiFullNum_[i].store(0, std::memory_order_relaxed);
//         }
//     }

//     int get(int nodeid, R_ID vid) {
//         if (atomic_test(vertIndex_[vid], nodeid)) return LOCAL;
//         if (atomic_test(vertNeiFullNum_[vid], 0))  return INTEGRAL;
//         return REMOTE;
//     }

//     void set(int nodeid, R_ID vid, int value) {
//         if (value == LOCAL) {
//             atomic_set(vertIndex_[vid], nodeid);
//         } else if (value == INTEGRAL) {
//             atomic_set(vertNeiFullNum_[vid], 0);
//         }
//     }

//     bitset<8> getNodeList(int vid) {
//         return bitset<8>(vertIndex_[vid].load(std::memory_order_relaxed));
//     }

//     void printBitMap() const {
//         for (R_ID i = 0; i < num_v_; ++i) {
//             std::cout << "bitmap[" << i << "]: ";
//             std::uint8_t bits = vertIndex_[i].load(std::memory_order_relaxed);
//             for (int j = 0; j < worker_num_; ++j)
//                 std::cout << ((bits >> j) & 1) << ' ';
//             std::cout << '\n';
//         }
//     }

//     void initBitMap(int type, const std::string& filename = "") {
//         if (type == 0) {
//             for (R_ID i = 0; i < num_v_; ++i)
//                 atomic_set(vertIndex_[i], i % worker_num_);
//         } else {
//             std::ifstream infile(filename);
//             if (!infile.is_open()) {
//                 std::cout << "Open bdgpartition.txt failure\n";
//                 std::exit(0);
//             }
//             std::string line;
//             while (std::getline(infile, line)) {
//                 std::istringstream iss(line);
//                 int x = 0;
//                 R_ID y;
//                 while (iss >> y) {
//                     if (y != static_cast<R_ID>(-1))
//                         atomic_set(vertIndex_[y], x);
//                     ++x;
//                 }
//             }
//         }
//     }
// };

// #endif // CONCURRENTBITMAP_H

#ifndef CONCURRENTBITMAP_H
#define CONCURRENTBITMAP_H

#include <atomic>
#include <vector>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <iostream>
#include <bitset>
#include <tbb/concurrent_vector.h>
// #include <tbb/spin_mutex.h> // 不再需要锁

using R_ID = unsigned;

#define REMOTE   0
#define LOCAL    1
#define INTEGRAL 3

class ConcurrentBitMap {
private:
    static constexpr size_t CACHE_LINE_SIZE = 64;

    int  worker_num_;
    R_ID num_v_;
    int  my_rank_;

    /* * 关于内存对齐的提示：
     * 如果内存紧张，建议去掉 alignas。只有当相邻索引被不同线程极其频繁地
     * 同时写入时，对齐才有意义。对于只读或低频写入，这会浪费 63/64 的内存。
     */
    struct AlignedAtomicU8 {
        std::atomic<std::uint8_t> value;
        AlignedAtomicU8() : value(0) {}
    };

    // 使用 atomic 自身保证线程安全
    tbb::concurrent_vector<AlignedAtomicU8> vertIndex_;
    tbb::concurrent_vector<std::atomic<std::uint8_t>> vertNeiFullNum_;

    // 辅助函数：直接使用硬件指令设置位，比 CAS 循环更快
    static void atomic_set_bit(std::atomic<std::uint8_t>& byte, int bit) {
        byte.fetch_or(1u << bit, std::memory_order_release);
    }

    // 辅助函数：清除位
    static void atomic_clear_bit(std::atomic<std::uint8_t>& byte, int bit) {
        byte.fetch_and(~(1u << bit), std::memory_order_release);
    }

    // 辅助函数：测试位
    static bool atomic_test_bit(const std::atomic<std::uint8_t>& byte, int bit) {
        return (byte.load(std::memory_order_acquire) & (1u << bit)) != 0;
    }

public:
    ConcurrentBitMap(int wk, R_ID nv, int rank)
        : worker_num_(wk), num_v_(nv), my_rank_(rank) {
        // 预分配空间，避免运行时动态扩容带来的开销
        vertIndex_.grow_to_at_least(num_v_);
        vertNeiFullNum_.grow_to_at_least(num_v_);
        
        // 初始化建议：如果 TBB vector 默认构造已清零，循环可省略
        // 但显式清零更安全
        for (R_ID i = 0; i < num_v_; ++i) {
            vertIndex_[i].value.store(0, std::memory_order_relaxed);
            vertNeiFullNum_[i].store(0, std::memory_order_relaxed);
        }
    }

    /* ---- 1. 获取顶点状态 (Lock-Free) ---- */
    int get(int nodeid, R_ID vid) {
        // 先读 LOCAL (vertIndex_)
        if (atomic_test_bit(vertIndex_[vid].value, nodeid)) {
            return LOCAL;
        }
        // 再读 INTEGRAL (vertNeiFullNum_)
        // 注意：第0位用于存储 INTEGRAL 标记
        if (atomic_test_bit(vertNeiFullNum_[vid], 0)) {
            return INTEGRAL;
        }
        
        return REMOTE;
    }

    /* ---- 2. 设置顶点状态 (Lock-Free) ---- */
    void set(int nodeid, R_ID vid, int value) {
        if (value == LOCAL) {
            atomic_set_bit(vertIndex_[vid].value, nodeid);
        } else if (value == INTEGRAL) {
            atomic_set_bit(vertNeiFullNum_[vid], 0);
        }
    }

    /* ---- 3. 检查并设置 (Lock-Free & Wait-Free) ---- */
    /* 返回 true 表示本次操作成功从 0 变为 1 (即之前没有设置过) */
    bool checkAndSet(R_ID vid, int value) {
        if (value == LOCAL) {
            std::uint8_t mask = (1u << my_rank_);
            // fetch_or 返回修改前的值
            std::uint8_t old = vertIndex_[vid].value.fetch_or(mask, std::memory_order_acq_rel);
            // 如果旧值中该位为0，说明是我们这次设置成功的
            return (old & mask) == 0;
        } else if (value == INTEGRAL) {
            std::uint8_t mask = 1u; // 第0位
            std::uint8_t old = vertNeiFullNum_[vid].fetch_or(mask, std::memory_order_acq_rel);
            return (old & mask) == 0;
        }
        return false;
    }

    /* ---- 4. 批量设置 (优化版) ---- */
    /* 移除了分组逻辑，直接并发写入，利用 CPU 缓存流水线 */
    void setBatch(const std::vector<R_ID>& vids, int value) {
        if (value == LOCAL) {
            int mask = (1u << my_rank_); // 假设 setBatch 默认设置 my_rank_，如果不是需要修改接口传参
            // 注意：原代码逻辑在 setBatch LOCAL 时似乎是设置 my_rank_
            // 但原代码 atomic_set 调用的是 my_rank_。这里保持一致。
            
            for (R_ID vid : vids) {
                // 不需要原子分组，硬件会自动处理并发一致性
                // memory_order_relaxed 在批量处理时性能更好，最后统一同步即可
                // 但为了安全，这里仍用 release
                vertIndex_[vid].value.fetch_or(mask, std::memory_order_release);
            }
        } else if (value == INTEGRAL) {
            for (R_ID vid : vids) {
                vertNeiFullNum_[vid].fetch_or(1u, std::memory_order_release);
            }
        }
    }

    /* ---- 5. 获取节点列表 ---- */
    std::bitset<8> getNodeList(R_ID vid) const {
        return std::bitset<8>(vertIndex_[vid].value.load(std::memory_order_acquire));
    }

    /* ---- 6. 快速检查 (Peek) ---- */
    int peek(R_ID vid) const {
        // 保持原逻辑：先看 NeiFull，再看 Index
        std::uint8_t neiBits = vertNeiFullNum_[vid].load(std::memory_order_relaxed);
        if ((neiBits & 0x01) != 0) {
            return INTEGRAL;
        }
        
        std::uint8_t indexBits = vertIndex_[vid].value.load(std::memory_order_relaxed);
        if ((indexBits & (1u << my_rank_)) != 0) {
            return LOCAL;
        }
        
        return REMOTE;
    }

    /* ---- 7. 打印位图 ---- */
    void printBitMap(R_ID start = 0, R_ID end = 10) const {
        if (end > num_v_) end = num_v_;
        for (R_ID i = start; i < end; ++i) {
            std::cout << "bitmap[" << i << "]: ";
            std::uint8_t bits = vertIndex_[i].value.load(std::memory_order_relaxed);
            for (int j = 0; j < worker_num_; ++j) {
                std::cout << ((bits >> j) & 1) << ' ';
            }
            std::cout << " | neiFull: " 
                      << (vertNeiFullNum_[i].load(std::memory_order_relaxed) & 0x01)
                      << '\n';
        }
    }

    /* ---- 8. 初始化位图 ---- */
    void initBitMap(int type, const std::string& filename = "") {
        if (type == 0) {
            for (R_ID i = 0; i < num_v_; ++i) {
                // 使用 relaxed 即可，初始化阶段通常是单线程或有屏障
                vertIndex_[i].value.store(1u << (i % worker_num_), std::memory_order_relaxed);
            }
        } else if (!filename.empty()) {
            std::ifstream infile(filename);
            if (!infile.is_open()) {
                std::cerr << "Open bdgpartition.txt failure\n";
                std::exit(EXIT_FAILURE);
            }
            
            std::string line;
            int nodeIndex = 0;
            while (std::getline(infile, line)) {
                std::istringstream iss(line);
                R_ID vid;
                while (iss >> vid) {
                    if (vid != static_cast<R_ID>(-1)) {
                         atomic_set_bit(vertIndex_[vid].value, nodeIndex);
                    }
                }
                nodeIndex++;
            }
        }
    }

    /* ---- 9. 重置位图 ---- */
    void reset() {
        for (R_ID i = 0; i < num_v_; ++i) {
            vertIndex_[i].value.store(0, std::memory_order_relaxed);
            vertNeiFullNum_[i].store(0, std::memory_order_relaxed);
        }
    }
    
    // 删除已废弃的接口声明，防止误用
    int getWithLock(R_ID vid) = delete;
    void setWithLock(R_ID vid, int value) = delete;
};

#endif // CONCURRENTBITMAP_H