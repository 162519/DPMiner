CC        = mpic++
CXXFLAGS  = -pthread -g -std=c++17
LDFLAGS   = -ltbb
# ① 优先用系统旧库 ② 静态链接 libstdc++ ③ 写死 rpath 防止被外部库拐跑
RPATH     = -Wl,-rpath,/usr/lib/gcc/x86_64-linux-gnu/7
STATIC    = -static-libstdc++ -static-libgcc

PMiner: ./src/dis_main.cpp
	$(CC) $(CXXFLAGS) $< -o ./bin/miner $(RPATH) $(STATIC) $(LDFLAGS)

