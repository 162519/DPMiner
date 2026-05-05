#include "/home/caohaoshuang/pjj/distributed_graph_query/PMiner-undirect/src/task_pool.h"
#include "/home/caohaoshuang/pjj/distributed_graph_query/PMiner-undirect/src/Task.h"
#include <iostream>
#include <random>
#include <algorithm>

int main() {
    SmallPool<Task> pool;
    
    std::cout << "初始状态:" << std::endl;
    std::cout << "  页数: " << pool.page_count() << std::endl;
    std::cout << "  总块数: " << pool.total_blocks() << std::endl;
    std::cout << "  每页块数: " << pool.blocks_per_page() << std::endl;

    /* 分配 10 万个 */
    std::vector<unsigned int> idx;
    idx.reserve(100000);
    
    for (int i = 0; i < 100000; ++i) {
        unsigned int id = pool.allocate();
        if (id == unsigned(-1)) {
            std::cerr << "分配失败 @ " << i << std::endl;
            break;
        }
        
        Task& d = const_cast<Task&>(pool.at(id));
        
        idx.push_back(id);
        
        if ((i + 1) % 10000 == 0) {
            std::cout << "已分配 " << (i + 1) << " 个, 页数: " 
                      << pool.page_count() << std::endl;
        }
    }
    for(int i=0;i<100;i++){
        std::cout<<idx[i]<<std::endl;
    }

    std::cout << "\n分配完成:" << std::endl;
    std::cout << "  成功分配: " << idx.size() << std::endl;
    std::cout << "  当前页数: " << pool.page_count() << std::endl;
    std::cout << "  当前总块数: " << pool.total_blocks() << std::endl;

    /* 随机释放一半 - 使用真正的随机数 */
    {
        std::random_device rd;  // 真正的随机数种子
        std::mt19937 gen(rd()); // Mersenne Twister 随机数生成器
        
        // 方法1: 打乱索引，然后释放前一半
        std::shuffle(idx.begin(), idx.end(), gen);
        
        std::cout << "\n随机释放一半 (" << idx.size() / 2 << " 个)..." << std::endl;
        
        for (size_t i = 0; i < idx.size() / 2; ++i) {
            pool.free(idx[i]);
        }
        
        // 统计已释放的索引
        std::vector<unsigned int> freed_indices(idx.begin(), idx.begin() + idx.size() / 2);
        std::vector<unsigned int> remaining_indices(idx.begin() + idx.size() / 2, idx.end());
        
        std::cout << "释放后:" << std::endl;
        std::cout << "  空闲块: " << pool.free_blocks() << std::endl;
        std::cout << "  内存效率: " << pool.memory_efficiency() * 100 << "%" << std::endl;
        
        // 验证释放的块确实被标记为空闲
        std::cout << "\n验证随机性:" << std::endl;
        std::cout << "  释放的最小索引: " << *std::min_element(freed_indices.begin(), freed_indices.end()) << std::endl;
        std::cout << "  释放的最大索引: " << *std::max_element(freed_indices.begin(), freed_indices.end()) << std::endl;
        std::cout << "  保留的最小索引: " << *std::min_element(remaining_indices.begin(), remaining_indices.end()) << std::endl;
        std::cout << "  保留的最大索引: " << *std::max_element(remaining_indices.begin(), remaining_indices.end()) << std::endl;
        
        /* 立即重用刚才释放的块 */
        std::cout << "\n重新分配 " << freed_indices.size() << " 个块..." << std::endl;
        
        std::vector<unsigned int> reallocated_indices;
        reallocated_indices.reserve(freed_indices.size());
        
        for (size_t i = 0; i < freed_indices.size(); ++i) {
            unsigned int id = pool.allocate();
            if (id == unsigned(-1)) {
                std::cerr << "重新分配失败 @ " << i << std::endl;
                break;
            }
            
            Task& d = const_cast<Task&>(pool.at(id));
            
            reallocated_indices.push_back(id);
        }
        
        // 分析重用情况
        std::cout << "\n重用分析:" << std::endl;
        std::cout << "  成功重分配: " << reallocated_indices.size() << std::endl;
        
        // 检查是否有重复分配（理论上不应该有）
        std::sort(reallocated_indices.begin(), reallocated_indices.end());
        auto duplicate_it = std::adjacent_find(reallocated_indices.begin(), reallocated_indices.end());
        if (duplicate_it != reallocated_indices.end()) {
            std::cout << "  警告: 发现重复索引 " << *duplicate_it << std::endl;
        }
        
        // 检查是否重用了已保留的块
        std::sort(remaining_indices.begin(), remaining_indices.end());
        int conflict_count = 0;
        for (unsigned int rid : reallocated_indices) {
            if (std::binary_search(remaining_indices.begin(), remaining_indices.end(), rid)) {
                conflict_count++;
            }
        }
        if (conflict_count > 0) {
            std::cout << "  警告: " << conflict_count << " 个重用块与保留块冲突" << std::endl;
        }
    }

    std::cout << "\n最终统计:" << std::endl;
    std::cout << "总块数 : " << pool.total_blocks() << std::endl;
    std::cout << "空闲块 : " << pool.free_blocks() << std::endl;
    std::cout << "内存效率 : " << pool.memory_efficiency() << std::endl;
    
    return 0;
}