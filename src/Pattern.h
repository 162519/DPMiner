#include <iostream>
#include <vector>
#include <map>
#include <cstring>                   //已更改
#include <fstream>
#include <sstream>
#include <cassert>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include "Degree.h"
#include <limits.h>
#include <cmath>
using namespace std;

typedef unsigned P_ID;
typedef unsigned R_ID;

class Pattern
{
private:
  int num_v;  // 图顶点个数
  int max_id; // 该节点的顶点最大id值
  int num_e;  // 图边条数

  vector<vector<P_ID>> P_adj;            // 模式图邻接矩阵，0代表两点之间没有边，1代表两点之间有边且未被访问过，2代表两点之间有边且已被访问过
  Degree *degree_P;                               // 模式图顶点信息
  vector<vector<vector<int> *>> elabel; // 模式图顶点标签，按升序排列
  vector<P_ID> minMatchIDs;                        // 模式图出入度最大的点；matchPR_expand中计算
  vector<P_ID> center_order;                       // 模式图中心点访问顺序
  int minMatchID;                                 // 初始最小匹配节点；从minMatchIDs中的点出发确定路径，最终确定minMatchID
  int need_full;                                  // 需要全排的行数
  bool need_full_no_dup_rem;                      // 全排是否需要去重，若分析后两两都没有交集，则直接全排，不用去重判断，true表示不用去重判断
  vector<int> full_index;                         // 需要全排PMR行的索引
  
  // findSym
  unordered_map<unsigned, int> sym;                    // 模式图中的等价点组,key为模式图id，value为等价点组编号，value相同的模式图点在同一等价点组中
  unordered_map<int, set<unsigned>> sym_group;         // key为等价点组，value为同组所有等价点
  vector<vector<unsigned>> Equivalent_order; // 二维数组，严格等价点排序数组
  bool isSym;                                          // 记录当前模式图是否自同构，如果自同构则需要进行同构检测
  int symNum;                                          // 自同构个数 自己挖掘自己
  int circleSize;                                      // 等价环顶点个数
  bool isEqCircle;                                     // 是否是等价环

  //constrained
  vector<vector<P_ID>> extend_order;                   // 拓展顺序
  vector<vector<P_ID>> antecedent_pid;                  // 前驱点
  vector<vector<vector<P_ID>>> successor_pid;           // 后继点
  vector<vector<vector<P_ID>>> str_successor_pid;       // 严格后继点

  // 预处理顶点，获取该分区顶点个数和max_id
  void pre_readv(string vinputfile)
  {
    num_v = 0;
    max_id = 0;
    ifstream vfile(vinputfile);
    string vline;
    assert(vfile.is_open());
    int vid;
    while (getline(vfile, vline))
    {
      istringstream ss(vline);
      ss >> vid;
      ++num_v;
      max_id = max(max_id, vid);
    }
  }

  // 构建degree_P, 初始化num_v, max_id
  void readv(string vinputfile)
  {
    // max_id = 10;
    // degree_P = nullptr;
    // if (degree_P)
    // {
    //   // 已经构建过degree_P
    //   cout << "The degree_P has been built." << endl;
    //   return;
    // }

    // 1 预处理顶点，获取该分区顶点个数和max_id
    pre_readv(vinputfile);

    // 2 读取顶点
    ifstream vfile(vinputfile);
    assert(vfile.is_open());
    string vline;
    num_v = 0;
    degree_P = new Degree[max_id + 1];
    int vid;
    while (getline(vfile, vline))
    {
      istringstream ss(vline);
      ss >> vid;
      ++num_v; // 顶点个数
      vector<int> vlabel;
      int vl; // 顶点属性
      while (ss >> vl)
      {
        vlabel.push_back(vl);
      }
      if(vlabel.size() > 0)
        degree_P[vid].vlabel = new vector<int>(vlabel);
    }
    // cout << "finish read vlabel." << endl;
    // cout << "max_id: " << max_id << endl;
    // cout << "num_v: " << num_v << endl;

    // // 测试
    // for (int i = 0; i <= max_id; ++i)
    // {
    //   cout << i << ": ";
    //   if (!degree_P[i].vlabel)
    //   {
    //     cout << "this vid is not in this subgraph." << endl;
    //     continue;
    //   }
    //   for (int j = 0; j < degree_P[i].vlabel->size(); ++j)
    //   {
    //     vector<int> *vlabel = degree_P[i].vlabel;
    //     cout << vlabel->at(j) << " ";
    //   }
    //   cout << endl;
    // }
  }

  // 构建P_adj, vlabel 初始化num_e
  void reade(string einputfile)
  {
    // 1 读取边
    ifstream efile(einputfile);
    assert(efile.is_open());
    string eline;
    num_e = 0;
    P_adj.resize(max_id + 1);
    elabel.resize(max_id + 1);
    for (int i = 0; i <= max_id; ++i)
    {
      P_adj[i].resize(max_id + 1);
      elabel[i].resize(max_id + 1);
      for (int j = 0; j <= max_id; ++j)
      {
        P_adj[i][j] = 0;
        elabel[i][j] = nullptr;
      }
    }
    int src, dst;
    while (getline(efile, eline))
    {
      istringstream ss(eline);
      ss >> src >> dst;
      ++num_e;
      // 读取边
      P_adj[src][dst] = 1;

      // 构建degree_P
      ++degree_P[src].outdeg;
      ++degree_P[dst].indeg;

      // 读取边标签
      vector<int> el;
      int telabel;
      while (ss >> telabel)
      {
        el.push_back(telabel);
      }
      if(el.size() > 0)
        elabel[src][dst] = new vector<int>(el);
    }
    // cout << "finish read elabel." << endl;
    // cout << "num_e: " << num_e << endl;

    // // 测试
    // for (int i = 0; i <= max_id; ++i)
    // {
    //   for (int j = 0; j <= max_id; ++j)
    //   {
    //     if (P_adj[i][j] == 1)
    //     {
    //       cout << i << " " << j << ": ";
    //       vector<int> *el = elabel[i][j];
    //       for (int k = 0; k < el->size(); ++k)
    //       {
    //         cout << el->at(k) << " ";
    //       }
    //       cout << endl;
    //     }
    //   }
    // }

    // for (int i = 0; i <= max_id; ++i)
    // {
    //   for (int j = 0; j <= max_id; ++j)
    //   {
    //     if (P_adj[i][j] == 1)
    //     {
    //       cout << i << " " << j << ": ";
    //       cout << P_adj[i][j] << endl;
    //     }
    //   }
    // }

    // for (int i = 0; i <= max_id; ++i)
    // {
    //   cout << i << ": ";
    //   if (!degree_P[i].vlabel)
    //   {
    //     cout << "this vid is not in this subgraph." << endl;
    //     continue;
    //   }
    //   cout << "indeg: " << degree_P[i].indeg << " outdeg: " << degree_P[i].outdeg << endl;
    // }
  }

  // 判断P_visited中是否还有点未访问
  bool PVAllVisited(vector<int> P_visited)
  {
    for (auto k : P_visited)
    {
      if (k == 0)
        return false;
    }
    return true;
  }

  void generatePermutationsExcludingOriginal(std::vector<int>& nums,vector<vector<vector<int>>>& group) {
    // 获取原始排列
    //std::vector<int> original(nums);
     vector<vector<int>> temp;
    // 生成全排列
    if(nums.size()==1){
      vector<int> sequence;
      sequence.emplace_back(nums[0]);
      temp.emplace_back(sequence);
    }
    else{
    //std::sort(nums.begin(), nums.end()); // 确保从有序状态开始生成排列
    for (int i = 0; i < nums.size(); ++i) {
        for (int j = i + 1; j < nums.size(); ++j) {
            // 复制原始序列以避免在原始序列上进行修改
            std::vector<int> newSequence(nums.begin(),nums.end());
            // 交换元素
            std::swap(newSequence[i], newSequence[j]);
            // 将新序列添加到集合中
            temp.emplace_back(newSequence);
            //newSequence.clear();
        }
    }
    }
    group.emplace_back(temp);
  }

  void combinePermutations(vector<int> adj_mat,const std::vector<std::vector<std::vector<int>>>& permutations, std::vector<vector<int>>& result,vector<vector<int>> &group, vector<int>& order,int depth) {
    if (depth == permutations.size()) {
        // 如果已经到达组的深度，复制当前排列到结果中
        vector<int>temp;
        vector<int> index(num_v,0);
        for(int i=0;i<order.size();i++){
         temp.emplace_back(group[order[i]][index[order[i]]]);
         index[order[i]]++;
        }
        bool flag = true;
        for (int i = 0; i < num_v; ++i){
            for (int j = i + 1; j < num_v; ++j){
                if (adj_mat[i*num_v+j] != 0){
                    if (adj_mat[temp[i]*num_v+temp[j]] == 0) // not isomorphism
                    {
                        flag = false;
                        break;
                    }
                }
            }
        }
        if (flag == true){
            result.push_back(temp);
        }
        return;
    }

    // 遍历当前组的所有排列
    for (const auto& perm : permutations[depth]) {
        // 将排列中的元素添加到当前排列
        group.emplace_back(perm);
        // 递归调用下一个组
        combinePermutations(adj_mat,permutations, result, group, order,depth + 1);
        // 回溯，为下一个排列做准备
        group.pop_back();
    }
  }

  void findSym_large(){
    std::vector<std::vector<P_ID>> P_adj_temp = P_adj;
    std::vector<std::vector<P_ID>> P_adj_mark(num_v, std::vector<P_ID>(num_v,0));
    Degree *degree_P_temp = (Degree *)calloc(num_v, sizeof(Degree));
    num_e = num_e*2;
    for(int i = 0; i < num_v; ++i){
      degree_P_temp[i].indeg = degree_P[i].indeg;
      degree_P_temp[i].outdeg = degree_P[i].outdeg;
    }
    for(int i = 0; i < P_adj.size(); ++i){
      for(int j = 0; j < P_adj[i].size(); ++j){
        if(P_adj[i][j] == 1 && P_adj_mark[i][j] == 0){
          P_adj_mark[i][j] = 1;
          P_adj_mark[j][i] = 1;
          P_adj[j][i] = 1;
          degree_P[j].outdeg++;
          degree_P[i].indeg++;
        }
      }
    }
    vector<int> adj_mat(num_v*num_v,0);
    for(int i=0;i<num_v;i++){
      for(int j=i+1;j<num_v;j++){
        if(P_adj[i][j]==1){
          adj_mat[i*num_v+j]=1;
          adj_mat[j*num_v+i]=1;
        }
      }
    }
    // print_pattern();

    // cout << "-----" << endl;
    vector<int> P_visited(num_v, 0);
    vector<int> order(num_v);
    int num=0;
    bool first=true;
    vector<vector<int>> group;
    P_ID id;
    while(num<num_v){
    for(int i=0;i<P_visited.size();i++){
        if(first==true&&P_visited[i]==0){
          P_visited[i]=1;
          id=i;
          vector<int> temp;
          temp.emplace_back(i);
          group.emplace_back(temp);
          order[i]=group.size()-1;
          first=false;
          num++;
        }
        else if(degree_P[i].indeg == degree_P[id].indeg && degree_P[i].outdeg == degree_P[id].outdeg && P_visited[i]==0){
          group.back().emplace_back(i);
          P_visited[i]=1;
          order[i]=group.size()-1;
          num++;
        }
        else{

        }
    }
    first=true;
    }

    vector<vector<vector<int>>> group_permu;
    for(int i=0;i<group.size();i++){
      generatePermutationsExcludingOriginal(group[i],group_permu);
    }
    vector<vector<int>> result;
    vector<vector<int>> group_candidata;
    combinePermutations(adj_mat,group_permu,result,group_candidata,order,0);
    // for(auto k:result){
    //   for(auto j:k){
    //     cout<<j<<" ";
    //   }
    //   cout<<endl;
    // }
    int setnum=1;
    bool flag=true;
    unordered_map<int,unordered_set<P_ID>> sym1;
    for(int i=0;i<result.size();i++){
      for(int j=0;j<result[0].size();j++){
          if(result[i][j]!=j){
            for(auto &k:sym1){
              if(k.second.find(j)!=k.second.end()||k.second.find(result[i][j])!=k.second.end()){
                k.second.insert(result[i][j]);
                k.second.insert(j);
                flag=false;
              }
              
            }
            if(flag==true){
            sym1[setnum].insert(j);
            sym1[setnum].insert(result[i][j]);
            setnum++;
            }
          }
          flag=true;
          
              
      }
    }
    for(const auto& k:sym1){
      sym_group[k.first]=std::set<P_ID>(k.second.begin(),k.second.end());
      
    }
    isSym = false;
    if(sym_group.size() > 0){
      isSym = true;
      cout<<"sym:yes"<<endl;
      }else
    {
      cout<<"sym:no"<<endl;
    }
    if(sym_group.size() == 1 && sym_group[1].size() == num_v && num_e/2 == num_v){
      isEqCircle = true;
      circleSize = num_v;
    }

    if (isEqCircle)
    {
        cout << "exist equivalent circle" << endl;
        cout << "circleSize: "<<circleSize << endl;

    }
    cout << "sym_group" << endl;
    for (auto it = sym_group.begin(); it != sym_group.end(); it++){
       cout<<it->first<<": ";
       for (auto iter = it->second.begin(); iter != it->second.end(); iter++){
         cout<<*iter<<" ";
       }
       cout<<endl;
    }

    // 将P_adj还原为原来的有向模式图，避免影响其它模块 sky20240115
    P_adj = P_adj_temp;
    degree_P = degree_P_temp;
    num_e = num_e/2;
    // print_P_adj(P_adj);

  }

  // 判断模式图中是否还有边没有访问
  bool isNextEPatternEmpty(vector<vector<P_ID>> &P_adj_copy)
  {
    for (unsigned i = 0; i < num_v; i++)
    {
      for (unsigned j = i + 1; j < num_v; j++)
      {
        if (P_adj_copy[i][j] == 1 || P_adj_copy[j][i] == 1)
          return false;
      }
    }
    return true;
  }

  // 判断两个点的标签是否相等
  bool isVlabelEqual(int id1, int id2)
  {
    vector<int> *v1 = degree_P[id1].vlabel;
    vector<int> *v2 = degree_P[id2].vlabel;
    if(v1 == nullptr && v2 == nullptr)
      return true;
    if (v1->size() != v2->size())
    {
      return false;
    }
    for (int i = 0; i < v1->size(); ++i)
    {
      if (v1->at(i) != v2->at(i))
      {
        return false;
      }
    }
    return true;
  }

  // 判断两条边的标签是否相等
  bool isElabelEqual(int id1, int id2, int id3, int id4)
  {
    vector<int> *e1 = elabel[id1][id2];
    vector<int> *e2 = elabel[id3][id4];
    if(e1 == nullptr && e2 == nullptr)
      return true;
    if (e1->size() != e2->size())
    {
      return false;
    }
    for (int i = 0; i < e1->size(); ++i)
    {
      if (e1->at(i) != e2->at(i))
      {
        return false;
      }
    }
    return true;
  }

  void findSym()
  {
      // 将模式图转换为双向边，以便识别无向图的等价点 sky20240115
      // print_P_adj(P_adj);
  
  
      std::vector<std::vector<P_ID>> P_adj_temp = P_adj;
      std::vector<std::vector<P_ID>> P_adj_mark(num_v, std::vector<P_ID>(num_v,0));
      Degree *degree_P_temp = (Degree *)calloc(num_v, sizeof(Degree));
      num_e = num_e*2;
      for(int i = 0; i < num_v; ++i){
        degree_P_temp[i].indeg = degree_P[i].indeg;
        degree_P_temp[i].outdeg = degree_P[i].outdeg;
      }
      for(int i = 0; i < P_adj.size(); ++i){
        for(int j = 0; j < P_adj[i].size(); ++j){
          if(P_adj[i][j] == 1 && P_adj_mark[i][j] == 0){
            P_adj_mark[i][j] = 1;
            P_adj_mark[j][i] = 1;
            P_adj[j][i] = 1;
            degree_P[j].outdeg++;
            degree_P[i].indeg++;
          }
        }
      }
      // print_pattern();
  
      // cout << "-----" << endl;
      vector<int> P_visited(num_v, 0);
      vector<vector<P_ID>> P_adjcp = P_adj;
      int setNum = 1; //等价点组数
      while (!PVAllVisited(P_visited))
      {
          int curID;
          for (int i = 0; i < num_v; i++)
          {
              if (P_visited[i] == 0)
              {
                  curID = i;
                  break;
              }
          }
          P_visited[curID] = 1;
          vector<int> nums; //可能与curID等价的点
          for (int i = 0; i < num_v; i++)
          {
              if (degree_P[i].indeg == degree_P[curID].indeg && degree_P[i].outdeg == degree_P[curID].outdeg && i != curID)
              {
                  nums.push_back(i);
              }
          }
          if (nums.empty())
              continue;
          //判断nums中的点是否与curID等价
          for (auto tmpid : nums)
          {
              vector<vector<unsigned>> PMRcpy(num_v);
              vector<int> selcpy(num_v);
              long long result = 0;
              bool branchFinish = true;
              int visited_edgeNum = 0;
              int oriID = tmpid;
              unordered_map<R_ID, int> isTraversed;
              // cout<<"in tmpid "<<tmpid<<"  curid "<<curID<<endl;
              sym_searchPG(PMRcpy, selcpy, P_adjcp, tmpid, curID, branchFinish, result, oriID, isTraversed);
              // cout<<"findSym---result"<<result<<endl;
              // cout<<"out tmpid "<<tmpid<<"  curid "<<curID<<endl;
              if (result != 0)
              {
                  P_visited[tmpid] = 1;
                  sym[tmpid] = setNum;
                  sym[curID] = setNum;
              }
          }
          setNum++;
      }
      // 1 构建sym_group
      for (auto it = sym.begin(); it != sym.end(); it++)
      {
          P_ID id = it->first;
          int group = it->second;
          sym_group[group].insert(id);
      }
  
      // 2 将等价点存储成二维vector 严格等价点排序数组
      int eqNum = sym.size();          //等价点总数
      int eqSetNum = sym_group.size(); //等价点组数
      int vecRow = num_v - eqNum + eqSetNum;
      //vector<vector<int>> eqVec;
      Equivalent_order.resize(vecRow);
      P_ID curid = 0;
      unordered_map<P_ID,int> mp;
      for (int i = 0; i < vecRow; i++)
      {
          while(mp.count(curid)!=0&&curid<num_v){
              curid++;
          }
          if (sym.count(curid) != 0)
          {
              int curSetNum = sym[curid];
              auto eqVertex = sym_group[curSetNum];
              for (auto it = eqVertex.begin(); it != eqVertex.end(); ++it)
              {
                  mp[*it]=1;
                  Equivalent_order[i].push_back(*it);
              }
          }
          else
          {
              mp[curid]=1;
              Equivalent_order[i].push_back(curid);
          }
      }
  
  
  
      // 3 是否自同构  有等价点则自同构
      isSym = false;
      if(sym.size() > 0){
        isSym = true;
        cout<<"sym:yes"<<endl;
        // cout<<"exist sym....."<<endl;
        // cout<<"Equivalent_order:"<<endl;
        // for(auto vec:Equivalent_order){
        //     for(auto k:vec){
        //         cout<<k<<" ";
        //     }
        //     cout<<endl;
        // }
      }else
      {
        cout<<"sym:no"<<endl;
      }
      
  
      // 4 判断是否有环
      // 等价点组中任意两点之间有边(就取等价点组的第一个和第二个点)，则该等价点组构成等价环
      // for (auto it = sym_group.begin(); it != sym_group.end(); it++)
      // {
      //     auto s = it->second;
      //     P_ID first = *(s.begin());
      //     P_ID second = *(++s.begin());
      //     // cout<<first<<",,,,,,,"<<second<<endl;
      //     if(P_adj[first][second] == 1 || P_adj[second][first] == 1){
      //       isEqCircle = true;
      //       circleSize = s.size();
      //     }
      // }
  
      // sky20231007
      // 只有一个等价点组，且等价点组的元素覆盖原模式图的所有顶点，且原模式图正好有n条边（有n个顶点）
      // 注意sym_group是一个unordered_map, 只有一个等价点组的时候键值为1
      cout<<num_v<<endl;
      if(sym_group.size() == 1 && sym_group[1].size() == num_v && num_e/2 == num_v){
        isEqCircle = true;
        //cout<<num_v;
        circleSize = num_v;
      }
  
      if (isEqCircle)
      {
          cout << "exist equivalent circle" << endl;
          cout << "circleSize: "<<circleSize << endl;
  
      }
  
  
      // 4 自同构个数，选择需要的等价点组进行约束去重
      //   等价点组大于1个时，需要如下操作
      //   选择自保留与起始点所在等价点组或者与起始点相连的等价点组 这样才能更早剪枝
      // if(sym_group.size() > 1){
      //   std::vector<unsigned> minMatchID_PPMR;
      //   for (R_ID j = 0; j < vertexNum_R; j++)
      //   {
      //       if (degree_P[j].indeg >= degree_P[minMatchID].indeg && degree_P[j].outdeg >= degree_P[minMatchID].outdeg)
      //       {
      //           minMatchID_PPMR.emplace_back(j);
      //           // R_visited[j] = 1;
      //       }
      //   }
      //   cout<<"=========================================================================="<<endl;
      //   cout<<"minMatchID_PPMR:"<<endl;
      //   for(int i = 0; i< minMatchID_PPMR.size();i++){
      //     cout<<minMatchID_PPMR[i]<<" ";
      //   }
      //   cout<<endl;
      //   symNum = 0;
      //   for (int i = 0; i < minMatchID_PPMR.size(); i++)
      //   {
      //         vector<vector<unsigned>> PMRcpy(vertexNum_P);
      //         vector<int> selcpy(vertexNum_P);
      //         long long result = 0;
      //         bool branchFinish = true;
      //         int visited_edgeNum = 0;
      //         int current_match_RID = minMatchID_PPMR[i];
      //         int oriID = current_match_RID;
      //         unordered_map<R_ID, int> isTraversed;
      //         isTraversed[oriID] = 1;
      //         // cout<<"in tmpid "<<tmpid<<"  curid "<<curID<<endl;
      //         sym_searchPG(PMRcpy, selcpy, P_adjcp, current_match_RID, minMatchID, branchFinish, result, oriID, isTraversed);
      //         symNum +=result;
      //   } 
      //   cout<<"symNum: "<<symNum<<endl;
      // }
      //测试
      // cout<<"Equivalent_order:"<<endl;
      // for(auto vec:Equivalent_order){
      //     for(auto k:vec){
      //         cout<<k<<" ";
      //     }
      //     cout<<endl;
      // }
      // //测试
      // cout << "sym:" << endl;
      // for (auto it = sym.begin(); it != sym.end(); ++it)
      // {
      //     cout << it->first << " " << it->second << endl;
      // }
      // //测试
      cout << "sym_group" << endl;
      for (auto it = sym_group.begin(); it != sym_group.end(); it++){
         cout<<it->first<<": ";
         for (auto iter = it->second.begin(); iter != it->second.end(); iter++){
           cout<<*iter<<" ";
         }
         cout<<endl;
      }
  
      // 将P_adj还原为原来的有向模式图，避免影响其它模块 sky20240115
      P_adj = P_adj_temp;
      degree_P = degree_P_temp;
      num_e = num_e/2;
      // print_P_adj(P_adj);
  }


  // 自同构挖掘
  void sym_searchPG(vector<vector<unsigned>> PMR_copy, vector<int> sel_copy, vector<vector<P_ID>> P_adj_copy, int current_match_RID, int current_match_PID, bool branchFinish, long long &result, int ori_centerID, unordered_map<unsigned, int> isTraversed)
  {
    // cout << "---------sym_searchPG----start--------------" << endl;
    // branchFinish = true;//初始化分支完整指标
    // 对于中心点Initialize模式图每顶点的选择度Sel;除开vp,s之外，其它模式图顶点选择度为无穷大
    if (current_match_RID == ori_centerID)
    {
      for (int i = 0; i < num_v; ++i)
      {
        if (i != current_match_PID)
        {
          sel_copy[i] = INT_MAX;
        }
      }
    }
    // 当前选择的中心点其PMR集合应当只包含一个值，这里来对其进行初始化
    /*PMR_copy[current_match_PID].resize(1);
    PMR_copy[current_match_PID][0] = current_match_RID;*/
    // 相比上面的方法，有更快的算法，如下，使用交换的方式
    vector<unsigned> temp;
    temp.emplace_back(current_match_RID);
    PMR_copy[current_match_PID].swap(temp);
    sel_copy[current_match_PID] = 1;

    // // 打印P_adj_copy里的数据
    // cout << "P_adj_copy:" << endl;
    // for (int i = 0; i < P_adj_copy.size(); i++)
    // {
    //   for (int j = 0; j < P_adj_copy[i].size(); j++)
    //   {
    //     cout << P_adj_copy[i][j] << " ";
    //   }
    //   cout << endl;
    // }
    // cout << endl;

    // 开始图匹配过程
    while (!isNextEPatternEmpty(P_adj_copy))
    {
      // cout << "---------while----start--------------" << endl;
      // int minSelId = getMaxSel_cur(current_match_PID);
      // 下面我们开始寻找从current_match_PID出发的所有未访问边，并对每条边做extend操作
      int neighborID = UINT_MAX; // 由这两个点构成最小匹配的边模式
      // int minMatchNum = INT_MAX;//neighborID的最小匹配数
      for (int i = 0; i < num_v; i++)
      {
        if (P_adj_copy[i][current_match_PID] == 1)
        {
          // minMatchNum = sel[i];
          neighborID = i;
          // cout << "开始逆向扩展 " << endl;
          sym_reverse_extendEdgePattern(current_match_PID, neighborID, current_match_RID, PMR_copy, sel_copy, P_adj_copy, branchFinish, isTraversed);
          if (branchFinish == false)
          {
            return;
          }
        }
      }
      for (int i = 0; i < num_v; i++)
      {
        if (P_adj_copy[current_match_PID][i] == 1)
        {
          // minMatchNum = sel[i];
          neighborID = i;
          // cout << "开始正向扩展 " << endl;
          sym_extendEdgePattern(current_match_PID, neighborID, current_match_RID, PMR_copy, sel_copy, P_adj_copy, branchFinish, isTraversed);
          if (branchFinish == false)
          {
            return;
          }
        }
      }

      // 如果上面的找边结束后neighborID为初始值(9294967296是我们支持的最大结点数)，那么证明current_match_PID的所有边都访问结束，需要重新计算current_match_PID，递归开始
      for (int i = 0; i < num_v; ++i)
      {
        for (int j = i + 1; j < num_v; ++j)
        {
          if ((P_adj_copy[i][j] == 1 || P_adj_copy[j][i] == 1) && (sel_copy[i] != INT_MAX || sel_copy[j] != INT_MAX))
          {
            if (sel_copy[i] <= sel_copy[j])
            {
              current_match_PID = i;
              neighborID = j;
            }
            else
            {
              current_match_PID = j;
              neighborID = i;
            }
            // 从这里开始非中心点的递归过程，也就是中心点所拓展的边已经全部匹配完成
            for (auto match_RID : PMR_copy[current_match_PID])
            {
              if (isTraversed.count(match_RID) == 0)
              {
                isTraversed[match_RID] = 1;
                sym_searchPG(PMR_copy, sel_copy, P_adj_copy, match_RID, current_match_PID, branchFinish, result, ori_centerID, isTraversed);
                isTraversed.erase(match_RID);
              }
            }
            branchFinish = false;
          }
        }
      }
      // cout << "---------while----end--------------" << endl;
    }

    // if (branchFinish == false)
    // {
    //   cout << "---------sym_searchPG----end--------------" << endl;
    //   return;
    // }
    // print_PMR(PMR_copy);
    long long cur_count = 1;
    for (unsigned i = 0; i < num_v; ++i)
    {
      cur_count *= PMR_copy[i].size();
    }
    result += cur_count;
    // cout << "result = " << result << endl;
    // cout << "---------sym_searchPG----end--------------" << endl;
    return;
  }

  // 逆向拓展
  void sym_reverse_extendEdgePattern(int v_pt, int v_ps, unsigned cur_r_vt, vector<vector<unsigned>> &PMR_copy, vector<int> &sel_copy, vector<vector<P_ID>> &P_adj_copy, bool &branchFinish, unordered_map<unsigned, int> isTraversed)
  {
    // cout << "---------sym_reverse_extendEdgePattern----start--------------" << endl;
    // cout << "(" << v_ps << "," << v_pt << ")"
    //      << "----" << cur_r_vt << endl;
    P_adj_copy[v_ps][v_pt] = 2; // 2代表两点间有边且已访问
    // 这里判断当前分支是否完整可以继续挖掘下去
    if (branchFinish == false)
      return;
    /* if(R_visited[cur_r_vt]==1&&cur_r_vt>ori_centerID)
    return;  */
    vector<unsigned> Mtemp;
    //!!!
    for (int i = 0; i < num_v; i++)
    {
      // cout<<"isTraversed: "<<endl;
      // for(auto it = isTraversed.begin(); it != isTraversed.end(); it++){
      //   cout<<it->first<<" "<<endl;
      // }
      if (P_adj_copy[i][cur_r_vt] != 0 && degree_P[i].indeg == degree_P[v_ps].indeg && degree_P[i].outdeg == degree_P[v_ps].outdeg && isVlabelEqual(i, v_ps) && isElabelEqual(i, cur_r_vt, v_ps, v_pt) && isTraversed.count(i) == 0)
      {
        Mtemp.push_back(i);
      }
    }
    // 当求得的点尚未计算PMR集，直接更新PMR集和sel集
    if (PMR_copy[v_ps].size() == 0)
    {
      PMR_copy[v_ps].swap(Mtemp);
      sel_copy[v_ps] = PMR_copy[v_ps].size();
    }
    // 当求得的点已经存在PMR集时，求交集，然后更新PMR集和sel集
    else
    {
      intersection(Mtemp, PMR_copy[v_ps]);
      sel_copy[v_ps] = PMR_copy[v_ps].size();
    }
    if (sel_copy[v_ps] == 0)
    {
      branchFinish = false;
    }
    // print_PMR(PMR_copy);
    // cout << "---------sym_reverse_extendEdgePattern----end--------------" << endl;
    return;
  }

  // 正向拓展
  void sym_extendEdgePattern(int v_ps, int v_pt, unsigned cur_r_vs, vector<vector<unsigned>> &PMR_copy, vector<int> &sel_copy, vector<vector<P_ID>> &P_adj_copy, bool &branchFinish, unordered_map<unsigned, int> isTraversed)
  {
    // cout << "---------sym_extendEdgePattern----start--------------" << endl;
    // cout << "(" << v_ps << "," << v_pt << ")"
    //      << "----" << cur_r_vs << endl;
    P_adj_copy[v_ps][v_pt] = 2; // 2代表两点间有边且已访问
    // 这里判断当前分支是否完整可以继续挖掘下去
    if (branchFinish == false)
      return;
    vector<unsigned> Mtemp;
    for (int i = 0; i < num_v; i++)
    {
      if (P_adj_copy[cur_r_vs][i] != 0 && degree_P[i].indeg == degree_P[v_pt].indeg && degree_P[i].outdeg == degree_P[v_pt].outdeg && isVlabelEqual(i, v_pt) && isElabelEqual(cur_r_vs, i, v_ps, v_pt) && isTraversed.count(i) == 0)
      {
        Mtemp.push_back(i);
      }
    }
    // 当求得的点尚未计算PMR集，直接更新PMR集和sel集
    if (PMR_copy[v_pt].size() == 0)
    {
      PMR_copy[v_pt].swap(Mtemp);
      sel_copy[v_pt] = PMR_copy[v_pt].size();
    }
    // 当求得的点已经存在PMR集时，求交集，然后更新PMR集和sel集
    else
    {
      intersection(Mtemp, PMR_copy[v_pt]);
      sel_copy[v_pt] = PMR_copy[v_pt].size();
    }
    if (sel_copy[v_pt] == 0)
    {
      branchFinish = false;
    }
    // print_PMR(PMR_copy);
    // cout << "---------sym_extendEdgePattern----end--------------" << endl;
    return;
  }

  // 打印PMR，用于测试
  void print_PMR(vector<vector<unsigned>> &PMR_copy)
  {
    cout << "Print current PMR collection." << endl;
    for (int i = 0; i < PMR_copy.size(); i++)
    {
      cout << "P" << i << ": ";
      for (auto j : PMR_copy[i])
      {
        cout << j << " ";
      }
      cout << endl;
    }
    cout << "Finish print PMR collection" << endl;
  }

  bool intersection(vector<unsigned> &Mtemp, vector<unsigned> &PMR_copy_oneline)
  {
    unordered_set<unsigned> temp(Mtemp.begin(), Mtemp.end());
    bool is_empty = true;
    // cout << "Intersection set is: ";
    vector<unsigned> intersect_result;
    for (auto i : PMR_copy_oneline)
    {
      auto p = temp.find(i);
      if (p != temp.end())
      {
        // cout << i << " ";
        temp.erase(i);
        intersect_result.emplace_back(i);
        is_empty = false;
      }
    }
    // 将求得的结果赋值给PMR_copy的当前行
    PMR_copy_oneline.swap(intersect_result);
    return is_empty;
  }

  // 起始点的确定
  bool matchPR_expand()
  {
    // 模式图总出入度最大的点作为起点候选点
    int degree = 0;
    int curd = 0;
    for (int i = 0; i < num_v; ++i)
    {
      curd = degree_P[i].indeg + degree_P[i].outdeg;
      if (curd == degree)
      {
        minMatchIDs.push_back(i);
      }
      else if (curd > degree)
      {
        minMatchIDs.clear();
        minMatchIDs.push_back(i);
        degree = curd;
      }
    }

     cout << "minMatchIDs: ";
     for (auto &a : minMatchIDs)
     {
       cout << a << " ";
     }
     cout << endl;

    return true;
  }
  struct ComparePairs {
    bool operator()(const std::pair<int, P_ID>& a, const std::pair<int, P_ID>& b) const {
        return a.first > b.first; // 从大到小排序
    }
};
struct PairCompare {
    bool operator()(const std::pair<int, int>& lhs, const std::pair<int, int>& rhs) const {
        if (lhs.first != rhs.first) {
            return lhs.first > rhs.first; // 首先根据第一个元素排序
        }
        return lhs.second > rhs.second; // 如果第一个元素相同，则根据第二个元素排序
    }
};
void center_order_dfs_large_new(P_ID preCenter, std::vector<std::vector<P_ID>>P_adj_copy, vector<P_ID> temp_center_order, int center_oreder_size, 
                                       int cur_order_sel, int & order_sel, vector<int> marked,unordered_set<P_ID>& no_candidata){
  marked[preCenter] = 2; //标记为中心点
  //std::set<pair<int, P_ID>,ComparePairs>  neibor;
  vector<P_ID> next_center; // 可以作为下一个中心的候选点
  int max=0;
  P_ID id;
  // 1 将中心点加入访问顺序
  // cout<<"preCenter: "<<preCenter<<"加入中心点"<<endl;
  temp_center_order.push_back(preCenter);
  // print_P_adj(P_adj_copy);

  // 2 访问与中心点相连的点
  int intersection_num = 0; // 记录当前扩展需要求交的次数
  int min_num=0;
  for(int i = 0; i< num_v; ++i){
    if(marked[i]==2){
      continue;
    }

    
    if(P_adj_copy[preCenter][i]!=0){ //存在边
      if(marked[i] == 1) 
      {
        ++intersection_num;
      }
      if(marked[i] == 0) {
        min_num++;
      }                  // 需要求交
      if(P_adj_copy[preCenter][i] == 1){
        P_adj_copy[preCenter][i] = 2; //标记为访问
        marked[i] = 1; //标记为需要求交
        //next_center.push_back(i); 
        // if((degree_P[i].indeg+degree_P[i].outdeg)>max){       这里修改了
        //   max=degree_P[i].indeg+degree_P[i].outdeg;
        //   id=i;
        // }
        //neibor.insert({degree_P[i].indeg+degree_P[i].outdeg,i});
      }
    }
    if(P_adj_copy[i][preCenter]!=0){ //存在边
      if(marked[i] == 1) 
      {
        ++intersection_num;
      }
      if(marked[i] == 0)  {
        min_num++;
      }   // 需要求交
      if(P_adj_copy[i][preCenter] == 1){
        P_adj_copy[i][preCenter] = 2; //标记为访问
        marked[i] = 1; //标记为需要求交
        //next_center.push_back(i);
        // if((degree_P[i].indeg+degree_P[i].outdeg)>max){       这里修改了
        //   max=degree_P[i].indeg+degree_P[i].outdeg;
        //   id=i;
        // }
        //neibor.insert({degree_P[i].indeg+degree_P[i].outdeg,i});
      }
    }

  }
  // print_P_adj(P_adj_copy);

  // 3 更新cur_order_sel和center_oreder_size    (intersection_num+1)加1是为了记录当前层，因为对于发散的图（PMiner中该部分的案例2）可能存在所有层都不用求交的情况 
  //    所有层都不用求交cur_order_sel则为0 第一次递归到底的情况就是最终结果了  递归层数的信息就丢失了  +1就保留了层数信息 不用求交单可以比较层数
  // cout<<"preCenter: "<<preCenter<<endl;
  // cout<<"intersection_num+1: "<<intersection_num+1<<endl;
  // cout<<"center_oreder_size: "<<center_oreder_size<<endl;
  // cout<<"(intersection_num+1)*pow(10,center_oreder_size): "<<(intersection_num+1)*pow(10,center_oreder_size)<<endl;
  cur_order_sel += (intersection_num*2+min_num)*pow(10,center_oreder_size);
  ++center_oreder_size;
  std::multimap<std::pair<int, int>, P_ID, PairCompare> candidata;
  int intersect=0,mine=0;
  if(!isNextEPatternEmpty(P_adj_copy)){
    for(int i=0;i<marked.size();i++){
      if(marked[i]==1){
        intersect=0;
        mine=0;
        for(int j=0;j<num_v;j++){
          if(P_adj_copy[i][j]==1||P_adj_copy[j][i]==1){
             mine++;
             if(marked[j]==1){
              intersect++;
             }
          }
          
        }
        candidata.insert(std::make_pair(std::pair<int, int>(intersect+mine,degree_P[i].indeg+degree_P[i].outdeg), i));
      }
    }
    auto k=*(candidata.begin());
    vector<P_ID> order;
    for(auto m:candidata){
      if(k.first.first==m.first.first&&k.first.second==m.first.second){
        //center_order_dfs_large_new(m.second, P_adj_copy, temp_center_order, center_oreder_size, cur_order_sel, order_sel, marked,no_candidata);
        order.push_back(m.second);
      }
      else{
        break;
      }
    }
    
    auto partitionPoint = std::partition(order.begin(), order.end(), [preCenter, P_adj_copy](P_ID i) {
        return (P_adj_copy[preCenter][i]!=0||P_adj_copy[i][preCenter]!=0);
    });
     for(P_ID k:order){
      //cout<<k<<endl;
       //center_order_dfs_large(k, P_adj_copy, temp_center_order, center_oreder_size, cur_order_sel, order_sel, marked,no_candidata);
       center_order_dfs_large_new(k, P_adj_copy, temp_center_order, center_oreder_size, cur_order_sel, order_sel, marked,no_candidata);
     }
    
    // 4 还有边没有访问则扩展下一个中心点
    

  }else{
    // 5 所有边都已经访问 将中心点最少且求交尽可能在外层的路线复制给全局中心点访问顺序， 即路径选择度最小的路径
    if(cur_order_sel < order_sel){
      // print_P_adj(P_adj_copy);
      // cout<<"temp_center_order: ";
      // for(int j = 0 ; j< temp_center_order.size() ; ++j){
      //   cout<<temp_center_order[j]<<" ";
      // }
      // cout<<endl;
      // cout<<"cur_order_sel: "<<cur_order_sel<<endl;
      center_order = temp_center_order;  // 更新最少中心点访问顺序序列
      order_sel = cur_order_sel; // 更新路径最小选择度
    }
  }
  return;


}

  void build_center_order_large(){
    matchPR_expand();
  // 1 寻找最优路劲
  int order_sel = INT32_MAX; // 路径选择度
  for(auto& minMatchID : minMatchIDs){
    std::vector<std::vector<P_ID>> P_adj_copy = P_adj;
    vector<P_ID> temp_center_order; 
    int center_oreder_size = 0;
    int cur_order_sel = 0;
    vector<int> marked(num_v, 0) ; // 0表示当前点有没有匹配点，1表示当前点有匹配点需要求交，2表示当前点是中心点，不需要求交
    unordered_set<P_ID> no_candidata;
    //center_order_dfs_large(minMatchID, P_adj_copy, temp_center_order, center_oreder_size, cur_order_sel, order_sel, marked,no_candidata);
    center_order_dfs_large_new(minMatchID, P_adj_copy, temp_center_order, center_oreder_size, cur_order_sel, order_sel, marked,no_candidata);
  }
  // center_order = {4,0,1,5};
  // center_order = {4,5,2,0};
  cout<<"center_order: ";
  for(int j = 0 ; j< center_order.size() ; ++j){
    cout<<center_order[j]<<" ";
  }
  cout<<endl;

  // 2 确定全局中心点
  // 将路径的起点作为中心点，赋值给全局中心点
  minMatchID = center_order[0];
  std::cout << "minMatchID = " << minMatchID << std::endl;

  // 3 计算需要全排的行数
  need_full = num_v-center_order.size();
  cout<<"need_full: "<<need_full<<endl;
  // 4 构建需要全排的PMR行的索引
  unordered_set<P_ID> co_temp(center_order.begin(),center_order.end());
  vector<int> temp;
  for(int i = 0; i < num_v; ++i){
    if(co_temp.count(i) == 0){
      temp.push_back(i);
    }
  }
  full_index = temp;
  cout<<"full_index: ";
  for(auto& a : full_index){
    cout<<a<<" ";
  }
  cout<<endl;


  // // 5 判断需要全排的索引行时候需要去重
  // // 5.1 需要全排的索引，用set便于判断受否存在
  // unordered_set<int> full_index_st(full_index.begin(), full_index.end());
  // // 5.2 收集所有中心点的的出入度点，入度点和出度点直接肯定求交为空，所以全排时不用去重判断
  // vector<vector<int>> idnofull(num_v); // idnofull[i]表示从iddegree收集的不用和顶点i求交的点集
  // //  收集中心点的出入度点
  // for(auto centerid : center_order){
  //   vector<vector<int>> iddegree(2); // 两行，0行表示入度，1行表示出度
  //   for(int i = 0; i < num_v; ++i){
  //     // 考察入度点
  //     if(P_adj[i][centerid] && full_index_st.count(i) != 0){ // 是入度点，且是需要全排的点，才参与收集
  //       iddegree[0].push_back(i);
  //     }
  //     // 考察出度点
  //     if(P_adj[centerid][i] && full_index_st.count(i) != 0){ // 是出度点，且是需要全排的点，才参与收集
  //       iddegree[1].push_back(i);
  //     }
  //   }
  //   // 将iddegree整理到idnofull中
  //   if()
  // }
  need_full_no_dup_rem = true;
  
}
  // 构建中心点访问顺序  挖掘完所有边的情况下使得作为中心点的点尽可能的少  前置条件：知道第一个中心点，起点：minMatchID
  void build_center_order()
  {
    matchPR_expand();
    // 1 寻找最优路劲
    int order_sel = INT32_MAX; // 路径选择度
    for (auto &minMatchID : minMatchIDs)
    {
      vector<vector<P_ID>> P_adj_copy = P_adj;
      vector<P_ID> temp_center_order;
      int center_order_size = 0;
      int cur_order_sel = 0;
      vector<int> marked(num_v, 0); // 0表示当前点有没有匹配点，1表示当前点有匹配点需要求交，2表示当前点是中心点，不需要求交
      center_order_dfs(minMatchID, P_adj_copy, temp_center_order, center_order_size, cur_order_sel, order_sel, marked);
    }
    // center_order = {4,0,1,5};
    // center_order = {4,5,2,0};
    // cout << "center_order: ";
    // for (int j = 0; j < center_order.size(); ++j)
    // {
    //   cout << center_order[j] << " ";
    // }
    // cout << endl;

    // 2 确定全局中心点
    // 将路径的起点作为中心点，赋值给全局中心点
    minMatchID = center_order[0];
    // cout << "minMatchID = " << minMatchID << endl;

    // 3 计算需要全排的行数
    need_full = num_v - center_order.size();
    // cout << "need_full: " << need_full << endl;
    // 4 构建需要全排的PMR行的索引
    unordered_set<int> co_temp(center_order.begin(), center_order.end());
    vector<int> temp;
    for (int i = 0; i < num_v; ++i)
    {
      if (co_temp.count(i) == 0)
      {
        temp.push_back(i);
      }
    }
    full_index = temp;
    // cout << "full_index: ";
    // for (auto &a : full_index)
    // {
    //   cout << a << " ";
    // }
    // cout << endl;

    // // 5 判断需要全排的索引行时候需要去重
    // // 5.1 需要全排的索引，用set便于判断受否存在
    // unordered_set<int> full_index_st(full_index.begin(), full_index.end());
    // // 5.2 收集所有中心点的的出入度点，入度点和出度点直接肯定求交为空，所以全排时不用去重判断
    // vector<vector<int>> idnofull(num_v); // idnofull[i]表示从iddegree收集的不用和顶点i求交的点集
    // //  收集中心点的出入度点
    // for(auto centerid : center_order){
    //   vector<vector<int>> iddegree(2); // 两行，0行表示入度，1行表示出度
    //   for(int i = 0; i < num_v; ++i){
    //     // 考察入度点
    //     if(P_adj[i][centerid] && full_index_st.count(i) != 0){ // 是入度点，且是需要全排的点，才参与收集
    //       iddegree[0].push_back(i);
    //     }
    //     // 考察出度点
    //     if(P_adj[centerid][i] && full_index_st.count(i) != 0){ // 是出度点，且是需要全排的点，才参与收集
    //       iddegree[1].push_back(i);
    //     }
    //   }
    //   // 将iddegree整理到idnofull中
    //   if()
    // }
    need_full_no_dup_rem = true;
  }

  // 递归访问模式图 在build_center_order中调动
  void center_order_dfs(int preCenter, vector<vector<P_ID>> P_adj_copy, vector<P_ID> temp_center_order, int center_order_size, int cur_order_sel, int &order_sel, vector<int> marked)
  {
    marked[preCenter] = 2;   // 标记为中心点
    vector<int> next_center; // 可以作为下一个中心的候选点
    // 1 将中心点加入访问顺序
    // cout<<"preCenter: "<<preCenter<<"加入中心点"<<endl;
    temp_center_order.push_back(preCenter);
    // print_P_adj(P_adj_copy);

    // 2 访问与中心点相连的点
    int intersection_num = 0; // 记录当前扩展需要求交的次数
    for (int i = 0; i < num_v; ++i)
    {
      if (P_adj_copy[preCenter][i] != 0)
      { // 存在边
        if (marked[i] == 1)
          ++intersection_num; // 需要求交
        if (P_adj_copy[preCenter][i] == 1)
        {
          P_adj_copy[preCenter][i] = 2; // 标记为访问
          marked[i] = 1;                // 标记为需要求交
          next_center.push_back(i);
        }
      }
      if (P_adj_copy[i][preCenter] != 0)
      { // 存在边
        if (marked[i] == 1)
          ++intersection_num; // 需要求交
        if (P_adj_copy[i][preCenter] == 1)
        {
          P_adj_copy[i][preCenter] = 2; // 标记为访问
          marked[i] = 1;                // 标记为需要求交
          next_center.push_back(i);
        }
      }
    }
    // print_P_adj(P_adj_copy);

    // 3 更新cur_order_sel和center_order_size    (intersection_num+1)加1是为了记录当前层，因为对于发散的图（PMiner中该部分的案例2）可能存在所有层都不用求交的情况
    //    所有层都不用求交cur_order_sel则为0 第一次递归到底的情况就是最终结果了  递归层数的信息就丢失了  +1就保留了层数信息 不用求交单可以比较层数
    // cout<<"preCenter: "<<preCenter<<endl;
    // cout<<"intersection_num+1: "<<intersection_num+1<<endl;
    // cout<<"center_order_size: "<<center_order_size<<endl;
    // cout<<"(intersection_num+1)*pow(10,center_order_size): "<<(intersection_num+1)*pow(10,center_order_size)<<endl;
    cur_order_sel += (intersection_num + 1) * pow(10, center_order_size);
    ++center_order_size;

    if (!isNextEPatternEmpty(P_adj_copy))
    {
      // 4 还有边没有访问则扩展下一个中心点
      if (next_center.size() > 0)
      { // 可以链式扩展
        // cout<<"有下一个中心点。。。。"<<endl;
        // 扩展
        for (int j = 0; j < next_center.size(); ++j)
        {
          center_order_dfs(next_center[j], P_adj_copy, temp_center_order, center_order_size, cur_order_sel, order_sel, marked);
        }
      }
      else
      { // 不能链式扩展完全图  则需要换非链式链接的中心点扩展，即从已经扩展为中心点的点中选择一个点，这个点还有边没有作为中心边扩展，则从这个点开始扩展
        // 满足这样条件的点可能有多个（已经为中心点且，还有边没有作为中心边访问）  所以需要先收集这些点，再依次递归考虑最优路径
        // 1)从中心点中移除当前点(preCenter)  因为走到这一步表明当前点没有边可以扩展了  从这个点再递归没有意义
        temp_center_order.pop_back();
        // 2)搜集符合条件的点
        vector<int> nect_extend; // 收集还有边没有作为中心边访问的中心点
        for (int i = 0; i < temp_center_order.size(); ++i)
        {
          int pid = temp_center_order[i];
          for (int j = 0; j < num_v; ++j)
          {
            if (P_adj_copy[pid][j] == 2 && marked[j] == 1)
            { // marked[j] == 2时表示作为中心边访问了
              nect_extend.push_back(j);
            }
            if (P_adj_copy[j][pid] == 2 && marked[j] == 1)
            { // marked[j] == 2时表示作为中心边访问了
              nect_extend.push_back(j);
            }
          }
        }
        // cout<<"nect_extend: ";
        // for(int j = 0 ; j< nect_extend.size() ; ++j){
        //   cout<<nect_extend[j]<<" ";
        // }
        // cout<<endl;
        // 3)换下一个中心点开始扩展
        for (int j = 0; j < nect_extend.size(); ++j)
        {
          center_order_dfs(nect_extend[j], P_adj_copy, temp_center_order, center_order_size, cur_order_sel, order_sel, marked);
        }
      }
    }
    else
    {
      // 5 所有边都已经访问 将中心点最少且求交尽可能在外层的路线复制给全局中心点访问顺序， 即路径选择度最小的路径
      if (cur_order_sel < order_sel)
      {
        // print_P_adj(P_adj_copy);
        // cout<<"temp_center_order: ";
        // for(int j = 0 ; j< temp_center_order.size() ; ++j){
        //   cout<<temp_center_order[j]<<" ";
        // }
        // cout<<endl;
        // cout<<"cur_order_sel: "<<cur_order_sel<<endl;
        center_order = temp_center_order; // 更新最少中心点访问顺序序列
        order_sel = cur_order_sel;        // 更新路径最小选择度
      }
    }
    return;
  }

  // 构建模式图的约束包含关系
  void build_constraint(){
    // 1 构建从每个点出发，分出入度，按从约束包含关系弱到约束包含关系强的访问顺序
    for(int i = 0 ; i< num_v; i++){
      //利用multimap的自动排序来排序约束包含点， int记录点的总出入度和，P_ID记录点，后续遍历即可得到按约束包含关系(总出入度和)大小的排序的点序
      multimap<int, P_ID>  outEdge;  // i点的出度点排序
      multimap<int, P_ID>  inEdge;  // i点的入度点排序
      for(int j = 0; j< num_v; j++){ 
        if(P_adj[i][j] == 1){ // 考察i的出度边
          outEdge.insert({degree_P[j].indeg+degree_P[j].outdeg, j});
        }
        if(P_adj[j][i] == 1){ // 考察i的入度边
          inEdge.insert({degree_P[j].indeg+degree_P[j].outdeg, j});
        }
      }
      vector<P_ID> outEdgeId; // i点的出度点序
      vector<P_ID> inEdgeId; // i点的入度点序
      for(auto it = outEdge.begin(); it!=outEdge.end(); it++){
        outEdgeId.push_back(it->second);
      }
      for(auto it = inEdge.begin(); it!=inEdge.end(); it++){
        inEdgeId.push_back(it->second);
      }
      extend_order.push_back(outEdgeId);
      extend_order.push_back(inEdgeId);
    }

    //=====================INFO============================
    // cout<<"extend_order: "<<endl;
    // for(int i = 0; i<extend_order.size()/2; i++){
    //   if(extend_order[i*2].size()> 0){
    //     cout<<i<<"的出度点：";
    //     for(int j = 0; j<extend_order[i*2].size(); j++){
    //       cout<<extend_order[i*2][j]<<" ";
    //     }
    //     cout<<endl;
    //   }
    //   if(extend_order[i*2+1].size()>0){
    //     cout<<i<<"的入度点：";
    //     for(int j = 0; j<extend_order[i*2+1].size(); j++){
    //       cout<<extend_order[i*2+1][j]<<" ";
    //     }
    //     cout<<endl;
    //   }
    // }
    //======================================================

    // 2 构建前继约束包含点，从i点出发，挖掘每个j节点时，每个j的约束包含点
    vector<vector<P_ID>> temp_antecedent(num_v, vector<P_ID>(num_v, INT_MAX)); //临时存储，方便初始化
    for(int i = 0; i<extend_order.size()/2; i++){
      for(int j = 0; j<extend_order[i*2].size(); j++){ // 考察出度边
        P_ID target = extend_order[i*2][j];// i的出度点
        if(j == 0){ //约束包含关系最弱的点 没有前继约束包含点 ，用INT_MAX表示
          temp_antecedent[i][target] = INT_MAX;
        }else{
          P_ID tempID = INT_MAX; // 记录j的前继约束包含点
          for(int k = 0; k<j; k++){ //找到j的前继约束包含点， 前继续约束包含点指约束包含点中约束包含关系最强的那个点
            P_ID ktarget = extend_order[i*2][k];// i的出度点 j的约束包含点
            if((degree_P[ktarget].indeg + degree_P[ktarget].outdeg) <= (degree_P[target].indeg + degree_P[target].outdeg)){ //找到j的约束包含点
              if(tempID == INT_MAX){//j当前没有约束包含点，则找到的点就是前继约束包含点
                tempID = ktarget;
              }else{ //已经有约束包含点，则要找到约束包含关系最强的点
                if((degree_P[ktarget].indeg + degree_P[ktarget].outdeg) > (degree_P[tempID].indeg + degree_P[tempID].outdeg)){
                  tempID = ktarget;
                }
              }
            }
          }
          temp_antecedent[i][target] = tempID;
        }
      }
      for(int j = 0; j<extend_order[i*2+1].size(); j++){ // 考察入度边
        P_ID target = extend_order[i*2+1][j];// i的出度点
        if(j == 0){ //约束包含关系最弱的点 没有前继约束包含点 ，用INT_MAX表示
          temp_antecedent[i][target] = INT_MAX;
        }else{
          P_ID tempID = INT_MAX; // 记录j的前继约束包含点
          for(int k = 0; k<j; k++){ //找到j的前继约束包含点， 前继续约束包含点指约束包含点中约束包含关系最强的那个点
            P_ID ktarget = extend_order[i*2+1][k];// i的出度点 j的约束包含点、
            if((degree_P[ktarget].indeg + degree_P[ktarget].outdeg) <= (degree_P[target].indeg + degree_P[target].outdeg)){ //找到j的约束包含点
              if(tempID == INT_MAX){//j当前没有约束包含点，则找到的点就是前继约束包含点
                  tempID = ktarget;
              }else{ //已经有约束包含点，则要找到约束包含关系最强的点
                if((degree_P[ktarget].indeg + degree_P[ktarget].outdeg) > (degree_P[tempID].indeg + degree_P[tempID].outdeg)){
                  tempID = ktarget;
                }
              }
            }
          }
          temp_antecedent[i][target] = tempID;
        }
      }
    }
    antecedent_pid = temp_antecedent;

    // cout<<"antecedent_pid: "<<endl;
    // for(int i =0; i<antecedent_pid.size()+1; i++){
    //   if(i == 0){
    //     cout<<"x ";
    //   }else{
    //     cout<<i-1<<" ";
    //   }
    // }
    // cout<<endl;
    // for(int i =0; i<antecedent_pid.size(); i++){
    //   cout<<i<<" ";
    //   for(int j =0; j<antecedent_pid[i].size();j++){
    //     if(antecedent_pid[i][j] == INT_MAX){
    //       cout<<"x ";
    //     }else{
    //       cout<<antecedent_pid[i][j]<<" ";
    //     }
    //   }
    //   cout<<endl;
    // }

    // 3 构建后继约束包含点，从i挖掘j时，j的约束包含哪些点
    vector<vector<vector<P_ID>>> temp_successor(num_v, vector<vector<P_ID>>(num_v,vector<P_ID>())); //临时存储，方便初始化
    for(int i = 0; i<num_v;i++){
      for(int j=0; j< num_v;j++){
        P_ID extendId = antecedent_pid[i][j];
        if( extendId != INT_MAX){
          temp_successor[i][extendId].push_back(j);
        }
      }
    }
    successor_pid = temp_successor;
    // cout<<"successor_pid:"<<endl;
    // for(int i = 0; i<num_v;i++){
    //   for(int j=0; j< num_v;j++){
    //     vector<P_ID> constraint_temp = successor_pid[i][j];
    //     if( constraint_temp.size() > 0){
    //       cout<<i<<"--->"<<j<<": ";
    //       for(int k = 0; k<constraint_temp.size(); k++ ){
    //         cout<<constraint_temp[k]<<" ";
    //       }
    //       cout<<endl;
    //     }
    //   }
    // }

    // 4 构建严格后继约束包含点
    vector<vector<vector<P_ID>>> temp_str_successor(num_v, vector<vector<P_ID>>(num_v,vector<P_ID>())); //临时存储，方便初始化
    for(int i = 0; i<num_v;i++){
      for(int j=0; j< num_v;j++){
        vector<P_ID> constraint_temp = successor_pid[i][j];
        if( constraint_temp.size() > 0){
          vector<P_ID> str_temp;
          for(int k = 0; k<constraint_temp.size(); k++ ){
            if(degree_P[constraint_temp[k]].indeg == degree_P[j].indeg && degree_P[constraint_temp[k]].outdeg == degree_P[j].outdeg){
              str_temp.push_back(constraint_temp[k]);//出度入度完全相同的点
            }
          }
          temp_str_successor[i][j].swap(str_temp);
        }
      }
    }
    str_successor_pid = temp_str_successor;
    // cout<<"str_successor_pid:"<<endl;
    // for(int i = 0; i<num_v;i++){
    //   for(int j=0; j< num_v;j++){
    //     vector<P_ID> constraint_temp = str_successor_pid[i][j];
    //     if( constraint_temp.size() > 0){
    //       cout<<i<<"--->"<<j<<": ";
    //       for(int k = 0; k<constraint_temp.size(); k++ ){
    //         cout<<constraint_temp[k]<<" ";
    //       }
    //       cout<<endl;
    //     }
    //   }
    // }
  }
  void print_partial_order(){
  cout<<"partial_order: "<<endl;
  for(int i = 0; i < num_v; ++i){
    cout<<i<<"\t";
  }
  cout<<endl;
  for(int i = 0; i < num_v; ++i){
    cout<<partial_order[i]<<"\t";
  }
  cout<<endl;

}

  void build_partial_order(){ // sky 20240115
  unordered_map<int, set<P_ID>> vertex_syms;
  // vector<P_ID> partial_order(num_v, -1); // -1表示没有偏序约束，如果partial_order[v] = u  那么M(u) < M(v)
  partial_order.assign(num_v, -1);
  for (auto it = sym_group.begin(); it != sym_group.end(); it++){
    // cout<<it->first<<": ";
    for (auto iter = it->second.begin(); iter != it->second.end(); iter++){
      // cout<<*iter<<" ";
      vertex_syms[*iter] = it->second;
    }
    // cout<<endl;
  }

  for(int i = 1; i < center_order.size(); ++i){
    P_ID cur = center_order[i];
    if(vertex_syms.count(cur)){
      // cur存在等价点
      for(int j = i-1; j>=0; --j){
        // 判断前面是否有其等价点
        P_ID tmp = center_order[j];
        if(vertex_syms[cur].count(tmp)){
          partial_order[cur] = tmp;
          break;
        }
      }
    }
  }

  print_partial_order();


}

void build_schedule(){          //得到挖掘过程中的等价组，利用等价组实现结果重用
  vector<unordered_set<P_ID>> equivalent_group; //记录每一轮挖掘过程的等价组
  std::vector<std::vector<P_ID>> P_adj_copy = P_adj; //复制模式图信息
  std::vector<std::vector<P_ID>> P_adj_copy1 = P_adj; //复制模式图信息
  vector<vector<unordered_set<P_ID>>> equivalent_group_schedule;
  equivalent_group_schedule.resize(center_order.size());
  needs_update_per_level.resize(center_order.size(), std::vector<bool>(num_v, false));
  min_schedule.resize(center_order.size());
  int_schedule.resize(center_order.size());
  vector<int> marked(num_v, 0) ;
  vector<int> marked1(num_v, 0) ;
  for(int i=0;i<center_order.size();i++){
    marked[center_order[i]] = 2;
    marked1[center_order[i]] = 2;
    P_ID current_id=center_order[i];
    unordered_map<P_ID,unordered_set<P_ID>> first_equivalent_group_temp;   //记录第一次被访问的点的等价性
    vector<unordered_set<P_ID>> equivalent_group_temp(equivalent_group.size()); //记录当前轮挖掘过程的等价组
    for(int j = 0; j< num_v; ++j){
      if(marked[j]==2){
      continue;
        }
      if(P_adj_copy[current_id][j]!=0){ //存在边
        if(marked[j] == 1) 
        {
          for(int m=0;m<equivalent_group.size();m++){
            if(equivalent_group[m].find(j)!=equivalent_group[m].end()){
              equivalent_group_temp[m].insert(j);
              break;
            }
          }
        }
        if(marked[j] == 0) 
        {
          first_equivalent_group_temp[degree_P[j].indeg+degree_P[j].outdeg].insert(j);
        }                  
        if(P_adj_copy[current_id][j] == 1){
          P_adj_copy[current_id][j] = 2; //标记为访问
          marked[j] = 1; //标记为需要求交
          needs_update_per_level[i][j] = true;
        }
      }
      if(P_adj_copy[j][current_id]!=0){ //存在边
        if(marked[j] == 1) 
        {
          for(int m=0;m<equivalent_group.size();m++){
            if(equivalent_group[m].find(j)!=equivalent_group[m].end()){
              equivalent_group_temp[m].insert(j);
              break;
            }
          }
        }
        if(marked[j] == 0)  {
          first_equivalent_group_temp[degree_P[j].indeg+degree_P[j].outdeg].insert(j);
        }   
        if(P_adj_copy[j][current_id] == 1){
          P_adj_copy[j][current_id] = 2; //标记为访问
          marked[j] = 1; //标记为需要求交
          needs_update_per_level[i][j] = true;  // 标记为需要更新（求交或扩展）
        }
    }
    }
    equivalent_group.clear();
    for(int m=0;m<equivalent_group_temp.size();m++){
      if(equivalent_group_temp[m].size()>=2){
        equivalent_group.emplace_back(equivalent_group_temp[m]);
      }
    }
    for(auto k:first_equivalent_group_temp){
      if(k.second.size()>=2){
        equivalent_group.emplace_back(k.second);
      }
    }
    equivalent_group_schedule[i]=equivalent_group;
    //得到min和int的点组
    vector<int> is_read(equivalent_group_schedule[i].size(),0);
    for(int j = 0; j< num_v; ++j){
      
      if(marked1[j]==2){
      continue;
        }
      if(P_adj_copy1[current_id][j]!=0){ //存在边
        if(marked1[j] == 1) 
        {
          if(equivalent_group_schedule[i].size()==0){
            int_schedule[i].emplace_back(j);
          }
          for(int m=0;m<equivalent_group_schedule[i].size();m++){
            if (equivalent_group_schedule[i][m].find(j) != equivalent_group_schedule[i][m].end()) {
              if (is_read[m] == 0) {
                int_schedule[i].emplace_back(j);
                is_read[m] = 1;
                break; // 一旦添加，立即跳出循环
              } else {
            // 如果已经处理过，继续检查下一个
                break;
              }
            }
            else{
              if(m==equivalent_group_schedule[i].size()-1){
                int_schedule[i].emplace_back(j);
              }
            }
          }
        }
        if(marked1[j] == 0) 
        {
          if(equivalent_group_schedule[i].size()==0){
            min_schedule[i].emplace_back(j);
          }
          for(int m=0;m<equivalent_group_schedule[i].size();m++){
            if (equivalent_group_schedule[i][m].find(j) != equivalent_group_schedule[i][m].end()) {
              if (is_read[m] == 0) {
                min_schedule[i].emplace_back(j);
                is_read[m] = 1;
                break; // 一旦添加，立即跳出循环
              } else {
            // 如果已经处理过，继续检查下一个
                break;
              }
            }
            else{
              if(m==equivalent_group_schedule[i].size()-1){
                min_schedule[i].emplace_back(j);
              }
            }
          }
        }                  
        if(P_adj_copy1[current_id][j] == 1){
          P_adj_copy1[current_id][j] = 2; //标记为访问
          marked1[j] = 1; //标记为需要求交
        }
      }
      if(P_adj_copy1[j][current_id]!=0){ //存在边
        if(marked1[j] == 1) 
        {
          if(equivalent_group_schedule[i].size()==0){
            int_schedule[i].emplace_back(j);
          }
          for(int m=0;m<equivalent_group_schedule[i].size();m++){
            if (equivalent_group_schedule[i][m].find(j) != equivalent_group_schedule[i][m].end()) {
              if (is_read[m] == 0) {
                int_schedule[i].emplace_back(j);
                is_read[m] = 1;
                break; // 一旦添加，立即跳出循环
              } else {
            // 如果已经处理过，继续检查下一个
                break;
              }
            }
            else{
              if(m==equivalent_group_schedule[i].size()-1){
                int_schedule[i].emplace_back(j);
              }
            }
          }
        }
        if(marked1[j] == 0)  {
          if(equivalent_group_schedule[i].size()==0){
            min_schedule[i].emplace_back(j);
          }
          for(int m=0;m<equivalent_group_schedule[i].size();m++){
            if (equivalent_group_schedule[i][m].find(j) != equivalent_group_schedule[i][m].end()) {
              if (is_read[m] == 0) {
                min_schedule[i].emplace_back(j);
                is_read[m] = 1;
                break; // 一旦添加，立即跳出循环
              } else {
            // 如果已经处理过，继续检查下一个
                break;
              }
            }
            else{
              if(m==equivalent_group_schedule[i].size()-1){
                min_schedule[i].emplace_back(j);
              }
            }
          }
        }   
        if(P_adj_copy1[j][current_id] == 1){
          P_adj_copy1[j][current_id] = 2; //标记为访问
          marked1[j] = 1; //标记为需要求交
        }
    }
    }

  }
  for (const auto& group : equivalent_group_schedule) {  //转成vector数组存储
        std::vector<std::vector<P_ID>> temp_group;
        for (const auto& set : group) {
            std::vector<P_ID> temp_set(set.begin(), set.end());
            sort(temp_set.begin(),temp_set.end());
            temp_group.push_back(temp_set);
        }
        equivalent_group_schedule_final.push_back(temp_group);
  }
  for (auto& group : min_schedule) {  //按照度数降序排列
        std::sort(group.begin(), group.end(), [this](const P_ID& a, const P_ID& b) {
            return this->compareByID(a, b); // 使用类的成员函数
        });
    }
  for (auto& group : int_schedule) {  //按照度数降序排列
        std::sort(group.begin(), group.end(), [this](const P_ID& a, const P_ID& b) {
            return this->compareByID_high(a, b); // 使用类的成员函数
        });
  }
  cout<<"equivalent_group"<<endl;
  for(int i=0;i<equivalent_group_schedule_final.size();i++){
    cout<<center_order[i]<<":";
    for(int j=0;j<equivalent_group_schedule_final[i].size();j++){
      for(int m=0;m<equivalent_group_schedule_final[i][j].size();m++){
        cout<<equivalent_group_schedule_final[i][j][m]<<" ";
      }
      cout<<endl;
    }
    cout<<endl;
  }
  cout<<"min_group"<<endl;
  for(int i=0;i<min_schedule.size();i++){
    cout<<center_order[i]<<":";
    for(int j=0;j<min_schedule[i].size();j++){
      cout<<min_schedule[i][j]<<" ";
    }
    cout<<endl;
  }
  cout<<"int_group"<<endl;
  for(int i=0;i<int_schedule.size();i++){
    cout<<center_order[i]<<":";
    for(int j=0;j<int_schedule[i].size();j++){
      cout<<int_schedule[i][j]<<" ";
    }
    cout<<endl;
  }
  for(int i=0;i<needs_update_per_level.size();i++){
    cout<<"level"<<i<<":";
    for(int j=0;j<needs_update_per_level[i].size();j++){
      if(needs_update_per_level[i][j]==true){
        cout<<j<<" ";
      }
    }
    cout<<endl;
  }
  P_adj.clear();
  P_adj.shrink_to_fit();
}

bool compareByID(const P_ID& a, const P_ID& b) {
  return (degree_P[a].indeg+degree_P[a].outdeg) > (degree_P[b].indeg+degree_P[b].outdeg);
}
bool compareByID_high(const P_ID& a, const P_ID& b) {
  return (degree_P[a].indeg+degree_P[a].outdeg) < (degree_P[b].indeg+degree_P[b].outdeg);
}           


public:
vector<int> partial_order;
std::vector<std::vector<P_ID>> min_schedule;  //每轮计划中不求交的点组
  std::vector<std::vector<P_ID>> int_schedule;   //每轮挖掘过程中需要求交的点组
  vector<vector<vector<P_ID>>> equivalent_group_schedule_final;  //每轮挖掘过程中的等价点组
  std::vector<std::vector<bool>> needs_update_per_level;
  Pattern(string vinputfile, string einputfile)
  {
    readv(vinputfile);
    reade(einputfile);
    if(num_v<8){
    findSym();
    }
    else{
     findSym_large();
    }
    build_center_order_large();
    build_constraint();
    build_partial_order();
    build_schedule();
    /***
     *环的特殊处理
    
    if(!isSym){
      build_constraint();
    }
    ***/
    cout << "finish build Pattern" << endl;
  }
  ~Pattern()
  {
    for (int i = 0; i <= max_id; ++i)
    {
      if (degree_P[i].vlabel)
      {
        delete degree_P[i].vlabel;
        degree_P[i].vlabel = nullptr;
      }
      for (int j = 0; j <= max_id; ++j)
      {
        if (elabel[i][j])
        {
          delete elabel[i][j];
          elabel[i][j] = nullptr;
        }
      }
    }
    if (degree_P)
    {
      delete[] degree_P;
      degree_P = nullptr;
    }
  }

  // 传入边(id1,id2)获取边(id1-->id2)属性
  vector<int> *getelabel(int id1, int id2)
  {
    return elabel[id1][id2];
  }

  // 传入顶点id获取顶点属性
  vector<int> *getvlabel(int id)
  {
    return degree_P[id].vlabel;
  }

  // 获取顶点个数
  int getnum_v()
  {
    return num_v;
  }

  // 获取当前要匹配的模式图PID
  int getcurrent_match_PID(int P_center_index)
  {
    return center_order[P_center_index];
  }
  // 获取center_oder的大小
  int getcenter_order_size()
  {
    return center_order.size();
  }



  //获取最小匹配点
  int getminMatchID()
  {
    return minMatchID;
  }

  int getneed_full()
  {
    return need_full;
  }

  int getfull(int i)
  {
    return full_index[i];
  }

  // 获取模式图degree结构
  Degree *getdegree_P()
  {
    return degree_P;
  }

  //返回正向拓展（出度）的约束包含点数组
  vector<P_ID> get_extend_order(int current_match_PID){
    return extend_order[current_match_PID*2];
  }
  //返回逆向拓展（入度）的约束包含点数组
  vector<P_ID> get_rev_extend_order(int current_match_PID){
    return extend_order[current_match_PID*2+1];
  }

};
