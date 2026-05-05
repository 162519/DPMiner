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
#include "tbb/concurrent_vector.h"
#include <tbb/spin_mutex.h>
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
  int my_rank; // 当前图分区的节点id
  //mutable tbb::spin_mutex update_mutex;  // 使用 TBB 的轻量级锁
  unsigned num_v; //图顶点个数
  unsigned max_id; // 该节点的顶点最大id值
  unsigned num_e; //图边条数
  
  tbb::concurrent_vector<unsigned> R_adj;         //正邻接表
  tbb::concurrent_vector<unsigned> R_reverse_adj; //逆邻接表
  tbb::concurrent_vector<unsigned> R_adjIndex;    //正邻接表索引  id1 id1邻接边在R_adj中的起始位置 id2 id2邻接边在R_adj中的起始位置
  tbb::concurrent_vector<unsigned> R_reverseAdjIndex;

  std::vector<Degree_R> degree_R;
  //unsigned *degree_R;
  //unsigned sizeReverseAdj;    //逆邻接表大小
  unsigned sizeAdj;       //正邻接表大小
  static inline tbb::spin_mutex                  g_resize_mtx; // 仅扩容时用
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
      // 初始化
      
      
      //sizeAdj=0;
      //R_reverse_adj.resize(num_e);
      R_adjIndex.resize(max_id + 1);
      //R_reverseAdjIndex.resize(max_id + 1);
      unsigned *R_adjIndex_tail = new unsigned[max_id + 1];
      //unsigned *R_reverseAdjIndex_tail = new unsigned[max_id + 1];
      //memset(R_reverseAdjIndex_tail, 0, max_id + 1);
      memset(R_adjIndex_tail, 0, max_id + 1);
      R_adjIndex[0] = 0;
      //R_reverseAdjIndex[0] = 0;
      R_adjIndex_tail[0] = 0;
      //R_reverseAdjIndex_tail[0] = 0;
      // 构建正逆邻接表
      for (unsigned i: localVer)
      { 
          R_adjIndex[i] = sizeAdj;
          //R_reverseAdjIndex[i] = sizeReverseAdj;
          R_adjIndex_tail[i] = R_adjIndex[i];
          //R_reverseAdjIndex_tail[i] = R_reverseAdjIndex[i];
          sizeAdj += degree_R[i].deg;
          //sizeReverseAdj += degree_R[i].indeg;
        
      }
      R_adj.resize(sizeAdj);
      //构建R_adj和R_reverse_adj
      ifstream efile(einputfile);
      string eline;
      assert(efile.is_open());
      unsigned from = 0, to = 0; // 起点id，终点id
      while (getline(efile, eline))
      {
          istringstream ss(eline);
          ss >> from;
          ss >> to;

          if(from != to){
            //只存储本地顶点的邻接数据
            if(degree_R[from].nodeid == my_rank)
              R_adj[R_adjIndex_tail[from]++] = to;
            if(degree_R[to].nodeid == my_rank){
              R_adj[R_adjIndex_tail[to]++] = from;
            }
            // R_adj[R_adjIndex_tail[from]++] = to;
            // R_reverse_adj[R_reverseAdjIndex_tail[to]++] = from;
          }

      }
      cout<<11111111111111111<<endl;
      for(unsigned i: localVer){
      //ascendingSort(&R_adj.front()+R_adjIndex[i], &R_adj.front()+R_adjIndex[i+1]);
      sort(R_adj.begin()+R_adjIndex[i], R_adj.begin()+R_adjIndex[i]+degree_R[i].deg);
    }

      // gout << "finish build adj." << endl;
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
    //释放内存空间
    concurrent_vector<unsigned>().swap(R_adj);
    concurrent_vector<unsigned>().swap(R_reverse_adj);
    concurrent_vector<unsigned>().swap(R_adjIndex);
    concurrent_vector<unsigned>().swap(R_reverseAdjIndex);
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

  //获取正领接表
  unsigned getR_adjIndex(unsigned i){
    return R_adjIndex[i];
  }
  unsigned getR_reverseAdjIndex(unsigned i){
    return R_reverseAdjIndex[i];
  }

  unsigned getR_adj(unsigned i){
    //tbb::spin_mutex::scoped_lock lock(update_mutex);
    assert(i < R_adj.size());
    return R_adj[i];
  }
  unsigned* getR_adj_data(){
    return  &R_adj[0];
  }

  unsigned getR_reverse_adj(unsigned i){
    return R_reverse_adj[i];
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
  //更新正向边和邻居点
  // bool update_forward(vector<vector<unsigned>> &vbuffer, vector<vector<unsigned>> &ebuffer, unsigned originID){
  //   /*
  //   vbuffer: 邻居节点的信息
  //   ebuffer: 邻居边的信息
  //   originID: 发出请求的顶点id
  //   */

  //   //打印vbuffer, ebuffer
  //   // gout<<"vbuffer: ";
  //   // for(int i = 0; i < vbuffer.size(); i++){
  //   //     for(int j = 0; j < vbuffer[i].size(); j++){
  //   //         gout<<vbuffer[i][j]<<" ";
  //   //     }
  //   //     gout<<endl;
  //   // }
  //   // gout<<"ebuffer: ";
  //   // for(int i = 0; i < ebuffer.size(); i++){
  //   //     for(int j = 0; j < ebuffer[i].size(); j++){
  //   //         gout<<ebuffer[i][j]<<" ";
  //   //     }
  //   //     gout<<endl;
  //   // }
    
  //   degree_R[originID].deg = 0;
  //   //degree_R[originID].indeg = 0;
  //   // update degree
  //   //read v
  //   for(int i = 0; i < vbuffer.size(); ++i){
  //     unsigned vid = vbuffer[i][0];
  //     if(degree_R[vid].nodeid == my_rank) continue;  //如果本地存在该顶点，就不更新
  //     unsigned nodeid = vbuffer[i][1];
  //     if(vbuffer[i].size() > 2){
  //       vector<int> vlabel;
  //       for(int j = 2; j < vbuffer[i].size(); ++j){
  //         vlabel.push_back(vbuffer[i][j]);
  //       }
  //       degree_R[vid].vlabel = new vector<int>(vlabel);
  //     }
  //     degree_R[vid].nodeid = nodeid;
  //     vertex.insert(vid);
  //   }
  //   //read e
  //   for(int i = 0; i < ebuffer.size(); ++i){
  //     unsigned long long from = ebuffer[i][0];
  //     unsigned long long to = ebuffer[i][1];

  //     if(from != to){
  //       //只存储本地顶点的邻接数据
  //       degree_R[from].deg++;  //from就是originID
  //       if(degree_R[to].nodeid != my_rank){
  //         degree_R[to].deg++;
  //       }
  //     }

  //     if(ebuffer[i].size() <= 2 )  continue; //如果只有两个元素，说明没有边标签
  //     from = (from + 1) << 32;
  //     unsigned long long elabelid = from + to;
  //     if(elabel.find(elabelid) != elabel.end()){
  //       continue;
  //     }
  //     vector<int>* elabelvalue = new vector<int>();
  //     for(int j = 2; j < ebuffer[i].size(); ++j){
  //       elabelvalue->push_back(ebuffer[i][j]);
  //     }
  //     if(elabelvalue->size() > 0)
  //       elabel[elabelid] = elabelvalue;

  //   }
    
    
  //   //update R_adjIndex  
  //   R_adjIndex[originID] = sizeAdj; //需要保存R_adj的大小
  //   //update R_adj
    
  //     //使用realloc函数增加空间
  //   R_adj.reserve(2*sizeAdj);
    
  //   if(sizeAdj+ebuffer.size()<R_adj.capacity()){
  //   for(int i =0; i < ebuffer.size(); ++i){
  //     R_adj[sizeAdj++] = ebuffer[i][1];
  //   }
  //   }
  //   else{
  //     R_adj.reserve(sizeAdj+3*ebuffer.size());
  //     for(int i =0; i < ebuffer.size(); ++i){
  //     R_adj[sizeAdj++] = ebuffer[i][1];
  //   }
  //   }

  //   // num_v = vertex.size();
  //   num_e = elabel.size();
  //   degree_R[originID].nodeid = my_rank;
  //   localVer.insert(originID);
  //   // degree_R[originID].nodeid = my_rank;
  //   return true;
  // }

  bool updata(unsigned* buffer, int count){
    
    //tbb::spin_mutex::scoped_lock lock(update_mutex);
    //auto t0 = std::chrono::steady_clock::now();
    int size = buffer[0];
    //unsigned old_sizeAdj = sizeAdj;

    // std::ostringstream oss;                    // 线程局部缓存，减少锁粒度
    // oss << "========== update called ==========\n";
    // oss << "old_sizeAdj=" << old_sizeAdj << '\n';
    // oss << "insert_size=" << (count - size - 1) << '\n';
    // oss << "insert_data=[";

    for(int i=0;i<size;i++){
      unsigned vid=buffer[i+1];
      //bitmap->set(my_rank, vid, LOCAL);
      //degree_R[vid].nodeid = my_rank;
      R_adjIndex[vid] = sizeAdj;
      sizeAdj += degree_R[vid].deg;
    }
    R_adj.grow_to_at_least(sizeAdj);
    // auto new_space = R_adj.grow_by(count - size -1);
    // std::copy(buffer +1 + size, buffer + count, new_space);
    auto newspace = R_adj.grow_by(count - size -1);
    std::copy(buffer +1 + size, buffer + count, newspace);
    //R_adj.reserve(sizeAdj);
    // for(int i=size+1;i<count;i++){
    //   R_adj[old_sizeAdj++]=buffer[i];
    //   oss << buffer[i] << (i + 1 < count ? ',' : ']');
    // }
    // oss << "\nnew_sizeAdj=" << sizeAdj << '\n'
    // << "R_adj.size()=" << R_adj.size() << "\n\n";
    // auto t1 = std::chrono::steady_clock::now();
    // auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    //g_total_us += us; 
    // oss << "elapsed_us=" << us << "\n\n";
    // std::ofstream ofs("/home/caohaoshuang/pjj/distributed_graph_query/PMiner-undirect_requestbuffer/log.txt", std::ios::app);
    //     ofs << oss.str();
    //memcpy(new_space, buffer + size + 1, (count - size -1)*sizeof(unsigned));
    return true;
  }

  // 一次处理一整批，避免多次 grow_by
bool updata_batch(const unsigned* buffer, int count, int start_vid, int size) {
  unsigned need = 0;
  for (int i = 0; i < size; ++i) {
      unsigned vid = buffer[i + 1];
      need += degree_R[vid].deg;
  }
  unsigned old_base = 0;
  {
      tbb::spin_mutex::scoped_lock lock(g_resize_mtx);
      old_base = sizeAdj;
      sizeAdj += need;
      auto newspace = R_adj.grow_by(need);
      std::copy(buffer +1 + size, buffer + count, newspace);
      unsigned local_off = old_base;
      for (int i = 0; i < size; ++i) {
          unsigned vid = buffer[i + 1];
          unsigned deg = degree_R[vid].deg;
          R_adjIndex[vid] = local_off;
          local_off += deg;
      }
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

