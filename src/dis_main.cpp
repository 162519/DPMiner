#include <iostream>
#include <unistd.h>
#include"tbb/tbb.h"
#include "PMiner.h"
#include "../util/global.h"
using namespace std;
using namespace tbb;

// g++ testPMiner.cpp -o testPMiner -ltbb
int main(int argc, char *argv[]){

  init_worker(&argc, &argv);
  init_timers();
  Profiler::instance().setRank(my_rank);

  //===========DEBUG================
  // int j=1;         
  // while(j){
  //   sleep(5);  // 陷入休眠，避免执行到程序异常处，导致中途退出
  // }
  //================================

  start_timer(LOAD_TIMER);

  //=======================================stable version================================
  if(argc != 6){
    printf("input error! usage: <method> <Graph> <Patternv> <Patterne> <precache_ratio>\n");
    printf("  precache_ratio: 0.0~1.0, proportion of high-degree remote vertices to pre-cache (0 = disabled)\n");
    exit(1);
  }
  string indexFile = "";  //更改划分策略后，可能需要根据划分的顶点文件生成位图
  string inputGraphv = "";
  string inputGraphe = "";
  string inputPatternv = "";
  string inputPatterne = "";
  int type = 0;

  string method = argv[1];
  string preGraph = argv[2];
  inputPatternv = argv[3];
  inputPatterne = argv[4];
  double precache_ratio = atof(argv[5]);

  if (method == "hash") {
    inputGraphv = preGraph + "/subvertex" + to_string(my_rank) +".txt";
    inputGraphe =  preGraph + "/subedge" + to_string(my_rank) +".txt";
  } else if (method == "bdg") {
    indexFile = preGraph + "/bitmap.txt";
    inputGraphv = preGraph + "/bdgsubvertex.txt";
    inputGraphe = preGraph + "/bdgsubedge.txt";
    type = 1;
  } else {
    printf("input error! unknown method!\n");
    exit(1);
  }
  //inputGraphe = argv[5];

  // //测试无复制图，无通信的挖掘
  //   inputGraphe=argv[5];
  //   inputGraphv = preGraph + "/subvertex" + to_string(my_rank) +".txt";
  //=======================================stable version================================

/*
  //======================================beta version============================================
  //！！！！！！注意先使用partition划分数据图，再使用PMiner！！！！！！
  string indexFile = "";  //更改划分策略后，可能需要根据划分的顶点文件生成位图
  int type = 0;
  //=============random evaluation================
  //real data graph
  // string inputGraphv = "/home/caohaoshuang/gr/distributed_graph_query/PMiner_v1.1/data/dataDec_wiki_vote/dataDec/subvertex" + to_string(my_rank) +".txt";
  // string inputGraphe = "/home/caohaoshuang/gr/distributed_graph_query/PMiner_v1.1/data/dataDec_wiki_vote/dataDec/subedge" + to_string(my_rank) +".txt";

  // string inputGraphv = "/home/caohaoshuang/gr/distributed_graph_query/PMiner_v1.1/data/dataDec_wiki_vote/dataDec/subvertex0.txt";
  // string inputGraphe = "/home/caohaoshuang/gr/distributed_graph_query/PMiner_v1.1/data/dataDec_wiki_vote/dataDec/subedge0.txt";
  //==============================================

  //=============bdg evaluation==================
  indexFile = "/home/caohaoshuang/gr/distributed_graph_query/PMiner_v1.1/data/bdgdataDec/bitmap.txt";
  string inputGraphv = "/home/caohaoshuang/gr/distributed_graph_query/PMiner_v1.1/data/bdgdataDec/bdgsubvertex.txt";
  string inputGraphe = "/home/caohaoshuang/gr/distributed_graph_query/PMiner_v1.1/data/bdgdataDec/bdgsubedge.txt";
  type = 1;
  //=============================================

  //pattern graph
  string inputPatternv = "/home/caohaoshuang/gr/distributed_graph_query/PMiner_v1.1/test/no3v.txt";
  string inputPatterne = "/home/caohaoshuang/gr/distributed_graph_query/PMiner_v1.1/test/no3e.txt";
  //======================================beta version============================================
*/

  Graph *g = new Graph(inputGraphv, inputGraphe, my_rank);
  // g->printInfo();
  cout<<111111<<endl;
  Pattern *p = new Pattern(inputPatternv, inputPatterne);
  
  int thn = 26;
  PMiner *pminer = new PMiner(g, p, thn, precache_ratio);
  //初始化位图  
  pminer->buildBitMap(type, indexFile, g->getnum_v(),my_rank);
  // pminer->printBitMap();

  stop_timer(LOAD_TIMER);
  printf("pro %d load time: %f s\n", my_rank, get_timer(LOAD_TIMER));
  
  worker_barrier();

  //start graph search 
  pminer->run();
  
  Profiler::instance().report();
  
  delete pminer;
  waitForAllPendingSends();
  worker_finalize();
  return 0;
}