// test_buffer.cpp
#include <iostream>
#include <vector>
#include <cstring>
#include <chrono>
#include "/home/caohaoshuang/pjj/distributed_graph_query/PMiner-undirect/src/BufferArena.h"

void testBufferArena() {
    using namespace std::chrono;

    BufferArena arena;
    constexpr int N = 100'000;               // 10 万个对象
    using Item = uint64_t;                   // 8 字节对象
    std::vector<void*> ptrs;
    ptrs.reserve(N);

    std::cout << "开始分配 " << N << " 个对象..." << std::endl;

    auto t0 = high_resolution_clock::now();

    for (int i = 0; i < N; ++i) {
        if (i % 10000 == 0) {
            std::cout << "已分配 " << i << " 个对象" << std::endl;
        }
        
        auto blk = arena.allocate(sizeof(Item), alignof(Item));
        if (blk.ptr == nullptr) {
            std::cerr << "allocate failed @" << i << "\n";
            return;
        }
        Item* p = new (blk.ptr) Item{i};     // placement new 写入
        ptrs.push_back(blk.ptr);
    }

    auto t1 = high_resolution_clock::now();

    /* 验证最后 10 个元素 */
    bool success = true;
    for (int i = N - 10; i < N; ++i) {
        Item* p = reinterpret_cast<Item*>(ptrs[i]);
        if (*p != static_cast<Item>(i)) { 
            std::cerr << "verify failed @" << i << " expected=" << i << " got=" << *p << "\n"; 
            success = false;
        }
    }

    auto t2 = high_resolution_clock::now();

    std::cout << "---- BufferArena 实测 ----\n";
    std::cout << "对象数  : " << N << "\n";
    std::cout << "总页数  : " << arena.total_reserved() / 65536 << " 页\n";
    std::cout << "总内存  : " << arena.total_reserved() / 1024.0 << " KB\n";
    std::cout << "分配耗时: " << duration_cast<microseconds>(t1 - t0).count() / 1000.0 << " ms\n";
    std::cout << "验证耗时: " << duration_cast<microseconds>(t2 - t1).count() / 1000.0 << " ms\n";
    std::cout << "验证结果: " << (success ? "通过" : "失败") << std::endl;
}

int main() {
    try {
        testBufferArena();
    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}