#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include <string>
#include <iomanip>

using namespace std;

int main(int argc, char *argv[]) {
    //注意修改参数！！！！！！！！！！
    string filename = "/home/caohaoshuang/gr/distributed_graph_query/PMiner_v1.1/log/bdg.web-Google.11-23_08-53.txt";
    const int NUM_QUERIES = 6;  // 你的测试中有5个查询
    const int NUM_PRO = 3;      // 每个查询有3个处理器

    ifstream inputFile(filename);  // 打开文件

    if (!inputFile.is_open()) {
        cerr << "Error opening file!" << endl;
        return 1;
    }

    // 用于存储每个查询的结果
    vector<long long unsigned> queryResults(NUM_QUERIES);
    // 用于存储每个查询的计算时间
    vector<double> queryTimes(NUM_QUERIES);

    string line;
    int currentQuery = -1;
    long long unsigned sum = 0;
    double maxTime = 0;
    // 逐行读取文件
    while (getline(inputFile, line)) {
        istringstream iss(line);
        // 判断是否是查询的开始
        if (line.find("query:") != string::npos) {
            currentQuery++;
            sum = 0;
            maxTime = 0;
        }

        // 读取 graph mining time elapse 行
        else if (line.find("graph mining time elapse") != string::npos) {
            // 找到数字的起始位置
            size_t start = line.find(":") + 1;

            // 提取数字部分
            string numberString = line.substr(start);

            // 去除字符串首尾的空格
            size_t firstNonSpace = numberString.find_first_not_of(" \t");
            size_t lastNonSpace = numberString.find_last_not_of(" \t");

            // 提取数字
            string finalNumberString = numberString.substr(firstNonSpace, lastNonSpace - firstNonSpace + 1);
            istringstream iss(finalNumberString);
            double miningTime;
            iss >> miningTime;
            // cout<<"miningTime: "<<miningTime<<endl;
            maxTime = max(maxTime, miningTime);
            queryTimes[currentQuery]=maxTime;
        }

        // 读取 mining result count 行
        else if (line.find("mining result count is") != string::npos) {
            // 找到数字的起始位置
            size_t start = line.find(":") + 1;

            // 提取数字部分
            string numberString = line.substr(start);

            // 去除字符串首尾的空格
            size_t firstNonSpace = numberString.find_first_not_of(" \t");
            size_t lastNonSpace = numberString.find_last_not_of(" \t");

            // 提取数字
            string finalNumberString = numberString.substr(firstNonSpace, lastNonSpace - firstNonSpace + 1);
            istringstream iss(finalNumberString);
            long long unsigned miningResultCount;
            iss >> miningResultCount;
            sum += miningResultCount;
            queryResults[currentQuery]=sum;
        }
    }

    // 关闭文件
    inputFile.close();

    // 打印结果
    cout<<"maxTimes: "<<endl;
    for (int i = 0; i < NUM_QUERIES; ++i) {
        std::cout << std::fixed << std::setprecision(3) << queryTimes[i] << std::endl;
    }
    cout<<"total count: "<<endl;
    for (int i=0;i<NUM_QUERIES;++i){
        std::cout << queryResults[i] << std::endl;
    }
    return 0;
}

/*
#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include <string>
using namespace std;

int main() {
    const int NUM_QUERIES = 6;  // 你的测试中有5个查询
    const int NUM_PRO = 3;      // 每个查询有3个处理器

    ifstream inputFile("/home/caohaoshuang/gr/distributed_graph_query/PMiner_v1.1/log/bdg.web-Google.11-24_01-31.txt");  // 打开文件

    if (!inputFile.is_open()) {
        cerr << "Error opening file!" << endl;
        return 1;
    }

    // 用于存储每个查询的结果
    vector<long long unsigned> queryResults(NUM_QUERIES);
    // 用于存储每个查询的计算时间
    vector<vector<double>> queryTimes(NUM_QUERIES, vector<double>(NUM_PRO, 0.0));

    string line;
    int currentQuery = -1;
    long long unsigned sum = 0;
    // 逐行读取文件
    while (getline(inputFile, line)) {
        istringstream iss(line);
        // 判断是否是查询的开始
        if (line.find("query:") != string::npos) {
            cout<<line<<endl;
            currentQuery++;
            sum = 0;
        }

        // 读取 graph mining time elapse 行
        else if (line.find("graph mining time elapse") != string::npos) {
            // cout<<line<<endl;
            // 找到数字的起始位置
            size_t start = line.find(":") + 1;

            // 提取数字部分
            string numberString = line.substr(start);

            // 去除字符串首尾的空格
            size_t firstNonSpace = numberString.find_first_not_of(" \t");
            size_t lastNonSpace = numberString.find_last_not_of(" \t");

            // 提取数字
            string finalNumberString = numberString.substr(firstNonSpace, lastNonSpace - firstNonSpace + 1);
            istringstream iss(finalNumberString);
            double miningTime;
            iss >> miningTime;
            // cout<<"miningTime: "<<miningTime<<endl;
            if (line.find("pro 0") != string::npos){
                queryTimes[currentQuery][0] += miningTime;
            }else if(line.find("pro 1") != string::npos) {
                queryTimes[currentQuery][1] += miningTime;
            } else if (line.find("pro 2") != string::npos) {
                queryTimes[currentQuery][2] += miningTime;
            }
        }

        // 读取 mining result count 行
        else if (line.find("mining result count is") != string::npos) {
            cout<<line<<endl;
            // 找到数字的起始位置
            size_t start = line.find(":") + 1;

            // 提取数字部分
            string numberString = line.substr(start);

            // 去除字符串首尾的空格
            size_t firstNonSpace = numberString.find_first_not_of(" \t");
            size_t lastNonSpace = numberString.find_last_not_of(" \t");

            // 提取数字
            string finalNumberString = numberString.substr(firstNonSpace, lastNonSpace - firstNonSpace + 1);
            istringstream iss(finalNumberString);
            long long unsigned miningResultCount;
            iss >> miningResultCount;
            cout<<"miningResultCount: "<<miningResultCount<<endl;
            sum += miningResultCount;
            queryResults[currentQuery]=sum;
        }
    }

    // 关闭文件
    inputFile.close();
    //打印queryResults和queryTimes
    for (int i = 0; i < NUM_QUERIES; ++i) {
        cout << queryResults[i] << " ";
    }
    cout << endl;
    for (int i = 0; i < NUM_QUERIES; ++i) {
        for (int j = 0; j < NUM_PRO; ++j) {
            cout << queryTimes[i][j] << " ";
        }
        cout << endl;
    }
    // 打印结果
    for (int i = 0; i < NUM_QUERIES; ++i) {
        double maxTime = 0;
        int maxTimePro = -1; // 记录最大时间的处理器

        // 遍历每个处理器
        for (int j = 0; j < NUM_PRO; ++j) {
            // 如果该处理器的时间大于最大时间，更新最大时间和对应的处理器
            if (queryTimes[i][j] > maxTime) {
                maxTime = queryTimes[i][j];
                maxTimePro = j;
            }
        }

        // 输出结果
        cout << "For query " << i + 3 << " max time " << maxTime << " and total count " << queryResults[i] << endl;
    }

    return 0;
}

*/