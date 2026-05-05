#include <iostream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <string>
#include <string>
#include <sstream>
using namespace std;

void sortData_NoLabel(string infilename, string outfilename){
    cout<<"start sortData_NoLabel"<<endl;
    ifstream infile(infilename);

    vector<pair<int, int>> data;
    int u, v;
    while (infile >> u >> v) {
        data.push_back(make_pair(u, v));
    }

    sort(data.begin(), data.end());

    ofstream outfile(outfilename);
    for (auto p : data) {
        outfile << p.first << " " << p.second << endl;
    }

    infile.close();
    outfile.close();
}

void generateVertexID(string inputfile, string outputfile){
    cout<<"start generateVertexID"<<endl;
    ifstream file(inputfile);
    string line;
    int maxVertexID = -1;
    int maxVertexLine = -1;
    int currentLine = 1;

    while (getline(file, line))
    {
        istringstream iss(line);
        int vertex1, vertex2;
        if (!(iss >> vertex1 >> vertex2))
        {
            // Error parsing line, skip to the next line
            continue;
        }

        // Update maxVertexID and maxVertexLine if necessary
        if (vertex1 > maxVertexID)
        {
            maxVertexID = vertex1;
            maxVertexLine = currentLine;
        }
        if (vertex2 > maxVertexID)
        {
            maxVertexID = vertex2;
            maxVertexLine = currentLine;
        }

        currentLine++;
    }

    file.close();

    cout << "Max Vertex ID: " << maxVertexID << endl;
    cout << "Max Vertex Line: " << maxVertexLine << endl;

    // 输出结果到文件中
    ofstream outfile(outputfile);
    // outfile << maxVertexID << endl;
    for(int i = 0; i <= maxVertexID; i++)
    {
        outfile << i << endl;
    }
}

int main() {
    // if(argc != 2)
    // {
    //     cout << "VertexData ERROR: <inputfile>" << endl;
    //     return 1;
    // }
    // string inputfile = argv[1];
    //处理顶点
    string path = "/home/caohaoshuang/gr/distributed_graph_query/PMiner_v1.1/data/dataDec_cit_Patents";
    string inputfile = "/home/caohaoshuang/gr/dataset/patents/cit-Patents.txt";
    string outputfile = path + "/vertexID.txt";
    generateVertexID(inputfile, outputfile);
    //处理边
    string infilename = "/home/caohaoshuang/gr/dataset/patents/cit-Patents.txt";
    string outfilename = path + "/sorted.txt";
    sortData_NoLabel(infilename, outfilename);

    return 0;
}