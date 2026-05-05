#ifndef GLOBAL_DEGREE_H
#define GLOBAL_DEGREE_H

#include "../util/global.h"
using namespace std;

// 收到请求数据并更新本地存储和索引
class global_degree
{
private:
    thread main_thread;
    std::atomic<bool> stop_flag{false};
public:
    global_degree(){
        main_thread = thread(&global_degree::run, this);
    }
    void stop() {
        stop_flag = true;
        if (main_thread.joinable()) {
            main_thread.join(); // 等待线程结束
        }
    }
    ~global_degree(){
        stop();
    }

    void handleData(unsigned * buffer, int count, int src){
        //Degree_global data;
        for(int i=0;i<count;i=i+2){
            g->updatadegree_R(buffer[i+1],buffer[i]);
        }
        delete[] buffer;
        //delete[]  data; 
        }

    void run(){
    	bool first = true;  //每次只接收一个请求
    	thread t;
    	
    	while(num<WORKER_NUM-1) //otherwise, thread terminates
    	{
    		int has_msg;
    		MPI_Status status;
            //MPI_Datatype msg_type = mpi_type;
    		MPI_Iprobe(MPI_ANY_SOURCE, tb_msg, MPI_COMM_WORLD, &has_msg, &status);
            //cout<<"zaijianshi"<<endl;
            //cout<<has_msg<<endl;
    		if(!has_msg) usleep(WAIT_TIME_WHEN_IDLE);   //没有消息就休眠
    		else
    		{
				int count;
				MPI_Get_count(&status, MPI_UNSIGNED, &count);
				unsigned *buffer = new unsigned[count];
				MPI_Recv(buffer, count, MPI_UNSIGNED, status.MPI_SOURCE, tb_msg, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
				unsigned *buffer_copy = new unsigned[count];
				memcpy(buffer_copy, buffer, count * sizeof(unsigned));  //必须要复制，才能保证多线程访问的数据是一致的
    			if(!first) t.join(); //wait for previous CPU op to finish; t can be extended to a vector of threads later if necessary
				
                t = thread(&global_degree::handleData, this, buffer_copy, count, status.MPI_SOURCE); //insert to q_resp[status.MPI_SOURCE]
                //cout<<num<<endl;
                num.fetch_add(1);
                first = false;
				delete[] buffer;
    		}
    	}
        //cout<<"jieshule"<<endl;
    	if(!first) t.join();
    }

};
#endif