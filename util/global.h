#ifndef GLOBAL_H
#define GLOBAL_H

#include <iostream>
#include <vector>
#include <cstdlib>
#include <time.h>
#include <mpi.h>
#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <malloc.h>
#include <cstdlib>
#include <utility>
#include <functional>
#include <stdlib.h>
#include <fstream>
//multi thread
#include <queue>
#include <chrono>
#include <thread>
#include <tbb/tbb.h>
#include <tbb/concurrent_unordered_set.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <memory>
#include "profiler.h"
#include "../src/OptmConcurrentBitMap.h"
#include "../src/Graph.h"
#include "../src/Cache.h"
#include "../src/Task.h"
using namespace std;
using namespace tbb;

typedef unsigned P_ID;
typedef unsigned R_ID;


//--------------------------------多线程相关---------------------------------
#define REQUEST_MSG 100
#define RESPONSE_MSG 101
#define STATUS_SYNC_CHANNEL 102
#define tb_msg 103
#define WAIT_TIME_WHEN_IDLE 1000 //us

#define MASTER_RANK 0

#define WAIT_TIME_WHEN_FULL 100 //us
#define init_batch_size 64
#define max_batch_size 512
#define min_batch_size 8
#define timeout_ms 50

#define PREFETCH_BATCH_SIZE 256

std::atomic<bool> isAllIdle(false);
std::atomic<int> num(0);

//--------------------------------多层任务队列---------------------------------
class LevelTaskQueues {
public:
    int maxLevel;
    vector<concurrent_queue<Task>> queues;
    std::unique_ptr<std::atomic<unsigned>[]> sizes;

    explicit LevelTaskQueues(int levels) : maxLevel(levels), 
        sizes(new std::atomic<unsigned>[levels]) {
        queues.resize(levels);
        for (int i = 0; i < levels; ++i)
            sizes[i].store(0, std::memory_order_relaxed);
    }

    void push(unsigned level, const Task& task) {
        queues[level].push(task);
        sizes[level].fetch_add(1, std::memory_order_release);
    }

    bool try_pop(unsigned level, Task& task) {
        if (queues[level].try_pop(task)) {
            sizes[level].fetch_sub(1, std::memory_order_release);
            return true;
        }
        return false;
    }

    unsigned size(unsigned level) const {
        return sizes[level].load(std::memory_order_acquire);
    }

    unsigned totalSize() const {
        unsigned total = 0;
        for (int i = 0; i < maxLevel; ++i)
            total += sizes[i].load(std::memory_order_acquire);
        return total;
    }

    bool empty() const {
        return totalSize() == 0;
    }
};

namespace std {
    // 定义哈希函数
    struct mySetHash {
        template <typename T>
        size_t operator()(const T& t) const {
            size_t h1 = std::hash<int>()(t.dest);
            size_t h2 = std::hash<int>()(t.type);
            size_t h3 = std::hash<unsigned>()(t.vid);
            size_t h4 = std::hash<unsigned>()(t.vid2);
            return h1 ^ h2 ^ h3 ^ h4;
        }
    };
    struct myEqual {
        template<typename T>
        bool operator()(const T& r1, const T& r2) const {
            return r1.dest == r2.dest && r1.type == r2.type && r1.vid == r2.vid && r1.vid2 == r2.vid2;
        }
    };
}

// 定义请求结构体
#define REQ_VERTEX 0
#define REQ_EDGE 1
#define REQ_NEIGHBOR 2
#define REQ_BITMAP 3

struct req_msg {
    int dest;
    int type;
    vector<unsigned> vid;
    
    // 构造函数 - 使用移动语义
    req_msg(int d, int t, std::vector<unsigned> v = {}) 
        : dest(d), type(t), vid(std::move(v)) {}  // ✅ 使用std::move
    
    // 使用默认的所有特殊成员函数
    req_msg(const req_msg&) = default;
    req_msg(req_msg&&) = default;
    req_msg& operator=(const req_msg&) = default;
    req_msg& operator=(req_msg&&) = default;
    ~req_msg() = default;
    
    // 或者完全删除自定义，让编译器生成所有
    // req_msg() = default;  // 如果需要默认构造函数
};

// 新增：消息边界头部
struct MsgHeader {
    uint32_t magic = 0xDEADBEEF;   // 魔数
    uint32_t bytes;                // 后面 payload 的字节数（不含头部）
};

//公用数据结构
ConcurrentBitMap *bitmap; // 用于存储当前子图的顶点id，用于判断顶点是否在当前子图中

Graph *g;   // 数据图

#define MAX_CACHE_SIZE 1005
Cache *cache; //热点数据缓存

LevelTaskQueues *levelQueues; //多层任务队列
std::atomic<unsigned> totalTaskCount{0}; //总任务数量
std::mutex g_dataReadyMtx; //数据就绪条件变量的互斥锁
std::condition_variable g_dataReadyCv; //数据就绪通知
int my_rank;
int my_size;

//--------------------------------预取批次---------------------------------
class PrefetchBatch {
    vector<Task> tasks_;
    unordered_set<unsigned> pendingVids_;
    bool dataReady_{false};

public:
    vector<Task>& tasks() { return tasks_; }
    const vector<Task>& tasks() const { return tasks_; }
    unordered_set<unsigned>& pendingVids() { return pendingVids_; }
    bool& dataReady() { return dataReady_; }

    void clear() {
        tasks_.clear();
        pendingVids_.clear();
        dataReady_ = false;
    }

    bool isReady() const {
        if (dataReady_) return true;
        for (unsigned vid : pendingVids_) {
            if (bitmap->get(my_rank, vid) != LOCAL) return false;
        }
        return true;
    }

    void waitForReady() {
        if (isReady()) return;
        std::unique_lock<std::mutex> lk(g_dataReadyMtx);
        g_dataReadyCv.wait(lk, [this]{ return isReady(); });
    }
};
//--------------------------------MPI相关---------------------------------
#define WORKER_NUM 3   //注意修改worker数量
vector<unsigned> request_num_to_other(WORKER_NUM,0);
vector<unsigned> response_num_from_other(WORKER_NUM,0);
unsigned request_num = 0;
tbb::concurrent_unordered_set<unsigned> requestedVids;

struct hash_pair { 
    template <class T1, class T2> 
    size_t operator()(const pair<T1, T2>& p) const
    { 
        auto hash1 = hash<T1>{}(p.first); 
        auto hash2 = hash<T2>{}(p.second); 
        return hash1 ^ hash2; 
    } 
}; 

void sendReq(req_msg r){    
    //发送请求
    MPI_Send(r.vid.data(), r.vid.size(), MPI_UNSIGNED, r.dest, REQUEST_MSG, MPI_COMM_WORLD);
    // gout << "processor " << my_rank << " send requset to " << r.dest << " type: " << r.type << " vid: " << r.vid << endl;
}

//获取远程数据
void getRemoteData(vector<unsigned>& batch){
    std::ofstream ofs("/home/caohaoshuang/pjj/distributed_graph_query/PMiner-undirect_mpi_new/tablesize_log.txt", std::ios::app);
    std::ostringstream oss;
    for(int i=0;i<batch.size();i++){
        oss<<batch[i]<<" ";
    }  
    oss<<endl;
    ofs << oss.str();
    vector<vector<unsigned>> destList(WORKER_NUM);
    for(int i = 0; i < batch.size(); i++){
        unsigned vid = batch[i];
        if(requestedVids.count(vid) > 0) continue;
        bitset<8> nodeList = bitmap->getNodeList(vid);
        vector<R_ID> candidate_node;
        for(int j = 0; j < WORKER_NUM; j++){
            if(j != my_rank && nodeList.test(j)){
                candidate_node.push_back(j);
            }
        }
        if(candidate_node.size() > 0){
            int dest = candidate_node[rand() % candidate_node.size()];
            destList[dest].push_back(vid);
            requestedVids.insert(vid);
        }
    }
    for(int i = 0; i < WORKER_NUM; i++){
        if(i != my_rank && destList[i].size() > 0){
            request_num_to_other[i]+=destList[i].size();
            Profiler::instance().incCounter(Profiler::C_REMOTE_REQS, destList[i].size());
            req_msg r(i, REQ_NEIGHBOR, std::move(destList[i])); 
            sendReq(r);
        }
    }
}

int getWorkerNum(string file){
    ifstream in(file);
    if(!in){
        cout << "hosts can't open. " << endl;
        exit(1);
    }
    int count = 0;
    string line;
    while(getline(in,line)){
        count++;
    }
    in.close();
    return count;
}

//int WORKER_NUM = getWorkerNum("/home/caohaoshuang/gr/distributed_graph_query/PMiner_v1.1/hosts.txt");




void gethost(char* hostname){
    gethostname(hostname, HOST_NAME_MAX);
}

void init_worker(int * argc, char*** argv)
{
	int provided;
	MPI_Init_thread(argc, argv, MPI_THREAD_MULTIPLE, &provided);
	if(provided != MPI_THREAD_MULTIPLE)
	{
	    printf("MPI do not Support Multiple thread\n");
	    exit(0);
	}
	MPI_Comm_size(MPI_COMM_WORLD, &my_size);
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
}

void worker_finalize()
{
    MPI_Finalize();
}

void worker_barrier()
{
    MPI_Barrier(MPI_COMM_WORLD); //only usable before creating threads
}

vector<unsigned> recv_mpi_data(int source, int tag){
    MPI_Status status;
    MPI_Probe(source, tag, MPI_COMM_WORLD, &status);
    int count;
    MPI_Get_count(&status, MPI_UNSIGNED, &count);
    std::vector<unsigned> buffer(count);
    MPI_Recv(buffer.data(), count, MPI_UNSIGNED, source, tag, MPI_COMM_WORLD, &status);
    return buffer;
}


std::mutex print_mutex; // 全局互斥量
// void printTask(Task& tk){
//     std::lock_guard<std::mutex> guard(print_mutex); // 锁定互斥量
//     gout << "PMR_copy: "<<endl;
//     for (int i = 0; i < tk.PMR_copy.size(); i++)
//     {
//         gout<<"["<<i<<"]: ";
//         for (int j = 0; j < tk.PMR_copy[i].size(); j++)
//         {
//             gout << tk.PMR_copy[i][j] << " ";
//         }
//         gout << endl;
//     }
    
//     gout << endl;
//     gout << "P_adj_copy: "<<endl;
//     for (int i = 0; i < tk.P_adj_copy.size(); i++)
//     {
//         for (int j = 0; j < tk.P_adj_copy[i].size(); j++)
//         {
//             gout << tk.P_adj_copy[i][j] << " ";
//         }
//         gout << endl;
//     }
//     gout << endl;
//     gout << "current_match_RID: " << tk.current_match_RID << endl;
//     gout << "P_center_index: " << tk.P_center_index << endl;
//     gout << "isTraversed: ";
//     for (auto i : tk.isTraversed) {
//         gout << i << " ";
//     }
//     gout << endl;
//     gout << "isprefetch: " << tk.isprefetch << endl;
// }

#endif