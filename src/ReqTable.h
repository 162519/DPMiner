#pragma once
#include <tbb/concurrent_vector.h>
#include <tbb/spin_mutex.h>
#include <vector>
#include <atomic>

using R_ID = unsigned;

#define REMOTE 0
#define REQUEST 2

class ReqTable
{
public:

static constexpr size_t MASK = 63;
tbb::concurrent_vector<tbb::concurrent_vector<unsigned>> reqTable;
tbb::concurrent_vector<std::atomic<bool>> vertexVisited;
std::atomic<unsigned> tableSize;

tbb::spin_mutex reqMutex[64];

    ReqTable(size_t max_vid)
      : reqTable(max_vid),
        vertexVisited(max_vid),
        tableSize(0)
    {
        for (auto& v : vertexVisited)
            v.store(false, std::memory_order_relaxed);
    }

    unsigned getsize(unsigned rid){
        return reqTable[rid].size();
    }

    void addTask(unsigned index, R_ID rid,
                 const std::vector<unsigned>& need_request)
    {
        tbb::spin_mutex::scoped_lock lk(reqMutex[rid & MASK]);
        reqTable[rid].push_back(index);
        tableSize.fetch_add(1, std::memory_order_relaxed);
    }

    void addTask(unsigned index, R_ID rid){
        tbb::spin_mutex::scoped_lock lk(reqMutex[rid & MASK]);
        reqTable[rid].push_back(index);
        tableSize.fetch_add(1, std::memory_order_relaxed);
    }

    void extractAndClear(R_ID rid, tbb::concurrent_vector<unsigned>& out)
    {
        tbb::spin_mutex::scoped_lock lk(reqMutex[rid & MASK]);
        out.swap(reqTable[rid]);
        tableSize.fetch_sub(out.size(), std::memory_order_relaxed);
    }

    void setVisited(R_ID rid)
    {
        vertexVisited[rid].store(true, std::memory_order_release);
    }

    int isRequest(R_ID rid)
    {
        bool expected = false;
        if(vertexVisited[rid].compare_exchange_strong(expected, true,
                                                       std::memory_order_acq_rel,
                                                       std::memory_order_acquire)){
            return REMOTE;
        }
        return REQUEST;
    }

private:
    
};
