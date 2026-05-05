#include <iostream>
#include <vector>                                         //已更改完
#include <map>
#include <set>
#include <cstring>
#include <fstream>
#include <sstream>
#include <cassert>
#include <algorithm>
#include "Degree.h"
#include <fstream>
#include <mutex>
#include <tbb/concurrent_hash_map.h>
#include <immintrin.h>   // _mm_prefetch
#include "PMiner.h"
//#include "../util/global.h"

using namespace std;
using namespace tbb;
static std::atomic<long long> g_total_us{0};
//--------------------------------输出文件相关---------------------------------

// 创建并打开文件
ofstream gout("out.log");


// 注意！注意！注意！vector是动态扩容的，尽量提前知道长度，用索引插入，不用push_back   如果是临时使用则不用考虑这些，但最终的作为存储的结构一定要考虑
// 在使用stl库其容器和方法的时候也要结合底层数据结构，考虑性能，选择最合适的方法和数据结构
// struct Degree
// {
//     unsigned indeg; // 入度数
//     unsigned outdeg; // 出度数
//     int nodeid; // 当前点所在节点id
//     vector<int>* vlabel; // 升序   注意在使用的时候需要判空，因为这个点可能不在当前子图中

//     Degree(){
//       indeg = 0;
//       outdeg = 0;
//       nodeid = 0;
//       vlabel = nullptr;  //初始化一定要置空，否则可能指向任意地方，会导致访问出错
//     }
// };
class Graph {
private:
  int my_rank;
  unsigned num_v;
  unsigned max_id; // 该节点的顶点最大id值
  unsigned num_e; //图边条数
  
  std::vector<unsigned> local_adj;        // 本地顶点的邻居数据（初始化后不变）
  std::vector<unsigned> local_adjIndex;   // 本地顶点的邻居偏移（初始化后不变）
  tbb::concurrent_hash_map<unsigned, std::vector<unsigned>> remote_adjIndex; // 远程顶点邻居数据

  std::vector<Degree_R> degree_R;
  unsigned sizeAdj;
  set<unsigned> vertex;  //当前顶点
  set<unsigned> localVer; //本地顶点
  tbb::concurrent_unordered_map<unsigned long long, vector<int>*> elabel; // 边标签   一边读入一边插入，保证有序性, string是边标签，标签也要按升序排列，便于和模式图标签对比

  // 读入顶点标签 构建rdegree.vlabel:必须保证顶点从0-n-1连续，否则会出错
  void readv(string vinputfile){
    // max_id = 10;

    // 1 读取顶点
    ifstream vfile(vinputfile);
    assert(vfile.is_open());

    // 1.1 读取max_id
    string vline;
    getline(vfile, vline);
    istringstream ss(vline);
    ss>>max_id;

    // 1.2 读取顶点属性
    // num_v = 0;
    degree_R.resize(max_id + 1);
    unsigned vid;
    while (getline(vfile, vline))
    {
      istringstream ss(vline);
      ss>>vid;
      // ++num_v; //顶点个数
      ss>>degree_R[vid].nodeid; // 顶点所在节点id
      vector<int> vlabel;
      int vl;// 顶点属性
      while (ss>>vl)
      {
        vlabel.push_back(vl);
      }
      if(vlabel.size() > 0)
        degree_R[vid].vlabel = new vector<int>(vlabel);
      vertex.insert(vid);   
      if(degree_R[vid].nodeid == my_rank)
        localVer.insert(vid);
    }
    //num_v值为maxid+1
    num_v = max_id + 1;
  }


// //用于测试全复制图
//   void readv(string vinputfile){
//     // max_id = 10;

//     // 1 读取顶点
//     ifstream vfile(vinputfile);
//     assert(vfile.is_open());

//     // 1.1 读取max_id
//     string vline;
//     getline(vfile, vline);
//     istringstream ss(vline);
//     ss>>max_id;

//     // 1.2 读取顶点属性
//     // num_v = 0;
//     degree_R.resize(max_id + 1);
//     unsigned vid;
//     while (getline(vfile, vline))
//     {
//       istringstream ss(vline);
//       ss>>vid;
//       // ++num_v; //顶点个数
//       int num5;
//       ss>>num5;
//       //ss>>degree_R[vid].nodeid; // 顶点所在节点id
//       degree_R[vid].nodeid=my_rank;
//       vector<int> vlabel;
//       // int vl;// 顶点属性
//       // while (ss>>vl)
//       // {
//       //   vlabel.push_back(vl);
//       // }
//       // if(vlabel.size() > 0)
//       //   degree_R[vid].vlabel = new vector<int>(vlabel);
//       vertex.insert(vid);   
//       //if(degree_R[vid].nodeid == my_rank)
//         localVer.insert(vid);
//     }
//     //num_v值为maxid+1
//     num_v = max_id + 1;
//   }
    // 创建正邻接表，逆邻接表，正邻接表索引，逆邻接表索引
  void build_adj(string einputfile)
  {
      local_adjIndex.resize(max_id + 1, 0);
      unsigned *adj_tail = new unsigned[max_id + 1];
      memset(adj_tail, 0, (max_id + 1) * sizeof(unsigned));
      adj_tail[0] = 0;
      for (unsigned i: localVer)
      { 
          local_adjIndex[i] = sizeAdj;
          adj_tail[i] = local_adjIndex[i];
          sizeAdj += degree_R[i].deg;
      }
      local_adj.resize(sizeAdj);
      ifstream efile(einputfile);
      string eline;
      assert(efile.is_open());
      unsigned from = 0, to = 0;
      while (getline(efile, eline))
      {
          istringstream ss(eline);
          ss >> from;
          ss >> to;

          if(from != to){
            if(degree_R[from].nodeid == my_rank){
              local_adj[adj_tail[from]] = to;
              adj_tail[from]++;
            }
            if(degree_R[to].nodeid == my_rank){
              local_adj[adj_tail[to]] = from;
              adj_tail[to]++;
            }
          }

      }
      delete[] adj_tail;
      for(unsigned i: localVer){
      sort(local_adj.begin()+local_adjIndex[i], local_adj.begin()+local_adjIndex[i]+degree_R[i].deg);
    }
  }
// void ascendingSort(unsigned* start, unsigned* end) {
//     // 使用比较函数对范围进行排序
//     std::sort(start, end, [](const unsigned& a, const unsigned& b) {
//         return a < b;
//     });
// }
  // 读入边标签 构建elabel 和 degree_R的出入度，
  void reade(string einputfile)
  {
      // 0 初始化
      num_e = 0;
      
      // 1 读取边标签,构建elabel和degree_R
      // degree_R = new Degree[max_id + 1];
      ifstream efile(einputfile);
      string eline;
      assert(efile.is_open());
      unsigned long long from , to ;
      while(getline(efile, eline))
      {
        num_e++;
        istringstream ss(eline);
        ss>>from;
        degree_R[from].deg++;
        ss>>to;
        degree_R[to].deg++;
        vector<int>* elabelvalue = new vector<int>();
        int el; //边属性
        while(ss>>el)
        {
          elabelvalue->push_back(el);
        }
        // printf("from = %llu, ", from);
        from = (from+1) << 32;
        unsigned long long elabelid = from + to;
        if(elabelvalue->size() > 0)
          elabel[elabelid] = elabelvalue;

        // printf("from = %llu, to = %llu, elabelid = %llu\n", from, to, elabelid);
      }
      build_adj(einputfile);
  }
public:
  // 构造函数
  Graph(string vinputfile, string einputfile, int rk) {
    sizeAdj = 0;
    //sizeReverseAdj = 0;
    my_rank = rk;
    readv(vinputfile);
    reade(einputfile);
    // cout << "finish build Graph" << endl;
  }
  ~Graph() {
    for(auto it = elabel.begin(); it != elabel.end(); ++it){
      delete it->second;
      it->second = nullptr;
    }
    for(unsigned i = 0; i <= max_id; ++i){
      if(degree_R[i].vlabel != NULL)
        delete degree_R[i].vlabel;
        degree_R[i].vlabel = nullptr;
    }
    std::vector<unsigned>().swap(local_adj);
    std::vector<unsigned>().swap(local_adjIndex);
    remote_adjIndex.clear();
    std::vector<Degree_R>().swap(degree_R);

  }

  
  // 返回id点的顶点标签
  // void getvlabel(unsigned id){
  // }
  // 返回start ---> target 的边标签
  vector<int>* getelabel(unsigned start, unsigned target){
    // printf("start = %u, target = %u ", start, target);
    unsigned long long left = start + 1;
    left = left << 32;
    unsigned long long elabelid = left + target;
    // gout<<"elabelid = "<<elabelid<<endl;
    if(elabel.find(elabelid) != elabel.end()) return elabel[elabelid];
    else return nullptr;
  }
  void updatadegree_R(unsigned deg,unsigned i){
    degree_R[i].deg=deg;
    
  }
  // 返回id的度数
  unsigned getdegree(unsigned id){
    assert(id<=max_id);
    return degree_R[id].deg;
  }

  unsigned getR_deg(unsigned id){
    assert(id<=max_id);
    return degree_R[id].deg;
  }
  // 返回id的出度个数
  // unsigned getR_outdeg(unsigned id){
  //   assert(id<=max_id);
  //   return degree_R[id].outdeg;
  // }
  // 返回id的入度个数
  // unsigned getR_indeg(unsigned id){
  //   assert(id<=max_id);
  //   return degree_R[id].indeg;
  // }

  // 获取id的顶点属性
  vector<int>* getvlabel(unsigned id){
    // gout<<"id = "<<id<<endl;
    assert(id<=max_id);
    return degree_R[id].vlabel;
  }

  // 返回id点的度数信息和顶点属性  慎用，因为属性vector是指针，可能为空，对外暴露不太合适
  // Degree* getdegree(unsigned id){
  //   return &degree_R[id];
  // }

  // 获取顶点个数
  int getnum_v(){
    return num_v;
  }

  // 获取边个数
  int getnum_e(){
    return num_e;
  }

  bool isLocal(unsigned vid) const {
    return vid <= max_id && degree_R[vid].nodeid == my_rank;
  }

  bool getLocalNeighbors(unsigned vid, const unsigned*& data, unsigned& len) {
    if (!isLocal(vid)) return false;
    unsigned start = local_adjIndex[vid];
    data = local_adj.data() + start;
    len = degree_R[vid].deg;
    return true;
  }

  bool getRemoteNeighbors(unsigned vid, const unsigned*& data, unsigned& len) {
    tbb::concurrent_hash_map<unsigned, std::vector<unsigned>>::const_accessor acc;
    if (remote_adjIndex.find(acc, vid)) {
        data = acc->second.data();
        len = acc->second.size();
        return true;
    }
    return false;
  }

  bool getNeighbors(unsigned vid, const unsigned*& data, unsigned& len) {
    if (isLocal(vid)) {
        return getLocalNeighbors(vid, data, len);
    }
    return getRemoteNeighbors(vid, data, len);
  }

  std::vector<Degree_R>& getdegree_R(){
    return degree_R;
  }

  int getnodeid_R(unsigned i){
    return degree_R[i].nodeid;
  }

  tbb::concurrent_unordered_map<unsigned long long, vector<int>*> getR_elabel(){
    return elabel;
  }
  bool updata_batch(const unsigned* buffer, int count, int start_vid, int size) {
  unsigned neighbor_offset = 1 + size;
  for (int i = 0; i < size; ++i) {
      unsigned vid = buffer[i + 1];
      unsigned deg = degree_R[vid].deg;
      std::vector<unsigned> neighbors(buffer + neighbor_offset,
                                       buffer + neighbor_offset + deg);
      sort(neighbors.begin(), neighbors.end());
      tbb::concurrent_hash_map<unsigned, std::vector<unsigned>>::accessor acc;
      remote_adjIndex.insert(acc, vid);
      acc->second = std::move(neighbors);
      neighbor_offset += deg;
  }
  return true;
}

  set<unsigned> getLocalVer(){
    return localVer;
  }

  unsigned get_local_num() {
    return localVer.size();
  }
  
};

