#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <numeric>
#include <random>
#include <algorithm>

#include "tree.h"
#include "hotree.h"
#include "DataReader.h"
#include "define.h"

using namespace std;
using namespace std::chrono;

// 数据集配置定义
struct DatasetConfig {
    string name;
    string dict_path;
    string data_path;
};

const int NUM_QUERIES = 50; // 增加样本数以获得更准确的平均值
const int FIXED_K = 3;
// const vector<int> N_VALUES = {1024, 2048}; 
const vector<int> N_VALUES = {1024, 32768};
// 定义四个数据集
vector<DatasetConfig> datasets = {
    {"yelp",       "../../dataset/yelp/keywords_dict.txt",       "../../dataset/yelp/dataset.txt"},
    {"tweets",     "../../dataset/tweets/keywords_dict.txt",     "../../dataset/tweets/dataset.txt"},
    {"foursquare", "../../dataset/foursquare/keywords_dict.txt", "../../dataset/foursquare/dataset.txt"},
    {"synthetic",  "../../dataset/synthetic/keywords_dict.txt",  "../../dataset/synthetic/dataset.txt"}
};
int main() {
    string result_filename = "../exp_result/query_vary_N.csv";
    
    ofstream csv(result_filename);
    csv << "Dataset,N,K,AvgTime_ms,AvgRounds,AvgVolume_Bytes,BlockSize\n";

    std::random_device rd;
    std::mt19937 gen(rd());

    #pragma omp parallel for
    for (const auto& ds : datasets) {
        cout << "\n>>> test dataset: " << ds.name << " (BlockSize=" << BlockSize << ")" << endl;
        
        // 1. 加载当前数据集的字典
        vector<string> dictionary = LoadDictionary(ds.dict_path);
        if (dictionary.empty()) {
            cerr << "Warning: can't load dictionary " << ds.dict_path << ", pass." << endl;
            continue;
        }

        for (int n : N_VALUES) {
            // 2. 加载 N 条数据用于构建
            vector<DataRecord> dataset = readDataFromDataset(ds.data_path, n);
            if (dataset.empty()) continue;

            // 3. 构建索引
            Client* client = nullptr;
            HOTree hotree(dictionary);
            hotree.Build(dataset, client);
            client = hotree.getClient();

            // 4. 从当前数据中随机选取查询
            vector<DataRecord> sampled_queries = readDataFromDataset(ds.data_path, NUM_QUERIES);
            std::shuffle(sampled_queries.begin(), sampled_queries.end(), gen);

            double total_time = 0;
            long long total_rounds = 0;
            long long total_volume = 0;
            int actual_queries = min((int)sampled_queries.size(), NUM_QUERIES);

            int start_rounds = client->communication_round_trip_;
            int start_volume = client->communication_volume_;
            for (int i = 0; i < actual_queries; ++i) {
                const auto& q = sampled_queries[i];

                auto start_t = high_resolution_clock::now();
                hotree.SearchTopK(q.x_coord, q.y_coord, q.processed_text, FIXED_K, client);
                auto end_t = high_resolution_clock::now();
                
                total_time += duration_cast<microseconds>(end_t - start_t).count() / 1000.0;
            }
            total_rounds += (client->communication_round_trip_ - start_rounds);
            total_volume += (client->communication_volume_ - start_volume);

            double avg_t = total_time / actual_queries;
            double avg_r = (double)total_rounds / actual_queries;
            double avg_v = (double)total_volume / actual_queries;

            // 写入 CSV: Dataset, N, K, Time, Rounds, Volume, BlockSize
            csv << ds.name << "," << n << "," << FIXED_K << "," << avg_t << "," << avg_r << "," << avg_v << "," << BlockSize << "\n";
            
            cout << "  [N=" << setw(5) << n << "] Time: " << fixed << setprecision(2) << avg_t 
                 << "ms | Rounds: " << avg_r << " | Vol: " << (long)avg_v << " Bytes" << endl;
        }
    }
    csv.close();
    return 0;
}