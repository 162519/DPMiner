#ifndef DEGREE_H
#define DEGREE_H

#include <iostream>
#include <vector>
using namespace std;

struct Degree
{
    unsigned indeg; // 入度数
    unsigned outdeg; // 出度数
    int nodeid; // 当前点所在的节点id
    vector<int>* vlabel; // 升序   注意在使用的时候需要判空，因为这个点可能不在当前子图中

    Degree(){
      indeg = 0;
      outdeg = 0;
      nodeid = -1;
      vlabel = nullptr;  //初始化一定要置空，否则可能指向任意地方，会导致访问出错
    }
};

struct Degree_R
{
    unsigned deg; // 度数
    //unsigned outdeg; // 出度数
    int nodeid; // 当前点所在的节点id
    vector<int>* vlabel; // 升序   注意在使用的时候需要判空，因为这个点可能不在当前子图中

    Degree_R(){
      // indeg = 0;
      // outdeg = 0;
      deg=0;
      nodeid = -1;
      vlabel = nullptr;  //初始化一定要置空，否则可能指向任意地方，会导致访问出错
    }
};

#endif // DEGREE_H