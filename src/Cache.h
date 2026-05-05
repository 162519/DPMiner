#ifndef CACHE_H
#define CACHE_H

#include <iostream>
#include "tbb/concurrent_queue.h"
#include <limits>
#include <atomic>
using namespace std;

typedef unsigned R_ID;
unsigned int cacheEmpty = std::numeric_limits<R_ID>::max();
std::atomic<int> cacheSize(0);

class Cache {
private:
    tbb::concurrent_bounded_queue<R_ID> cache;
    int capacity;
    
public:
    Cache(int _capacity): capacity(_capacity) {
        cache.set_capacity(capacity);
    }
    //使用前需要去重
    R_ID get(){
        R_ID val;
        if(cacheSize == 0){
            return cacheEmpty;
        }
        if(cache.try_pop(val)){
            cacheSize.fetch_sub(1);
            return val;
        }else{
            return cacheEmpty; //表示当前没有数据
        }
    }
    void put(R_ID val) {
        if(cache.try_push(val)){
            cacheSize.fetch_add(1);
        }
    }
};

#endif