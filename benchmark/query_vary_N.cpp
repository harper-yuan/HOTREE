#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <numeric>

#include "tree.h"
#include "hotree.h"
#include "DataReader.h"
#include "define.h" // 引入 BlockSize 定义

using namespace std;
using namespace std::chrono;

const int NUM_QUERIES = 2; 
string result_filename = "../exp_result/query_vary_N.csv";
string dictionary_filename = "../../dataset/keywords_dict.txt";
string queries_filename = "../../dataset/query.txt";
int k = 3;
// vector<int> n_values = {1024, 2048, 4096, 8192, 16384}; 
vector<int> n_values = {1024, 2048}; 

int main() {
    // 加载基础字典和查询集
    vector<string> dictionary = LoadDictionary(dictionary_filename);
    vector<DataRecord> all_queries = readDataFromDataset(queries_filename);
    
    ofstream csv(result_filename);
    csv << "N,K,AvgTime_ms,AvgRounds,AvgVolume_Bytes,BlockSize\n";

    // 测试不同的数据规模 N
    

    cout << "--- 执行实验 A (固定 K=" << k << ", BlockSize=" << BlockSize << ") ---" << endl;

    for (int n : n_values) {
        vector<DataRecord> data = readDataFromDataset("../../dataset/synthetic_dataset.txt", n);
        
        Client* client = nullptr;
        HOTree hotree(dictionary);
        hotree.Build(data, client);
        client = hotree.getClient();

        double total_time = 0;
        long long total_rounds = 0;
        long long total_volume = 0;

        for (int i = 0; i < NUM_QUERIES; ++i) {
            const auto& q = all_queries[i % all_queries.size()];

            // 记录查询前的累加值
            int start_rounds = client->communication_round_trip_;
            int start_volume = client->communication_volume_;

            auto start_t = high_resolution_clock::now();
            hotree.SearchTopK(q.x_coord, q.y_coord, q.processed_text, k, client);
            auto end_t = high_resolution_clock::now();

            total_time += duration_cast<microseconds>(end_t - start_t).count() / 1000.0;
            total_rounds += (client->communication_round_trip_ - start_rounds);
            total_volume += (client->communication_volume_ - start_volume);
        }

        double avg_t = total_time / NUM_QUERIES;
        double avg_r = static_cast<double>(total_rounds) / NUM_QUERIES;
        double avg_v = static_cast<double>(total_volume) / NUM_QUERIES;

        csv << n << "," << k << "," << avg_t << "," << avg_r << "," << avg_v << "," << BlockSize << "\n";
        cout << "N=" << setw(6) << n << " | Time: " << fixed << setprecision(3) << avg_t 
             << "ms | Rounds: " << avg_r << " | Vol: " << avg_v << " Bytes" << endl;
    }
    csv.close();
    return 0;
}