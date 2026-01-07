#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <numeric>
#include <random>
#include <algorithm>
#include <cmath>
#include <omp.h> // 移除 OpenMP

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

const int NUM_QUERIES = Z; // 样本数
const int FIXED_K = 1;

// const vector<int> N_VALUES = {1024, 2048}; 
const vector<int> N_VALUES = {(int)pow(2,16)};

// 定义四个数据集
vector<DatasetConfig> datasets = {
    // {"yelp",       "../../dataset/yelp/keywords_dict.txt",       "../../dataset/yelp/dataset.txt"},
    // {"tweets",     "../../dataset/tweets/keywords_dict.txt",     "../../dataset/tweets/dataset.txt"},
    // {"foursquare", "../../dataset/foursquare/keywords_dict.txt", "../../dataset/foursquare/dataset.txt"},
    {"synthetic",  "../../dataset/synthetic/keywords_dict.txt",  "../../dataset/synthetic/dataset.txt"}
};

int main() {
    string result_filename = "../exp_result/query_vary_N.csv";
    ofstream csv(result_filename);
    csv << "Scheme,Dataset,N,K,AvgTime_ms,AvgRounds,AvgVolume_Bytes,BlockSize,Access,Self_Access\n";

    std::random_device rd;
    std::mt19937 gen(rd());

    cout << ">>> 开始测试 Vary N (单线程)" << endl;
    // #pragma omp parallel for
    // 依照 query_vary_k 的逻辑，外层循环数据集，内层循环变量 N
    for (const auto& ds : datasets) {
        cout << "\n>>> 正在测试数据集: " << ds.name << endl;

        for (int n : N_VALUES) {
            // 加载字典
            vector<string> dictionary = LoadDictionary(ds.dict_path);
            if (dictionary.empty()) {
                cerr << "Warning: can't load dictionary " << ds.dict_path << endl;
                continue;
            }

            // 加载 N 条数据
            vector<DataRecord> dataset = readDataFromDataset(ds.data_path, n);
            if (dataset.empty()) {
                continue;
            }

            // 构建索引
            Client* client = nullptr;
            HOTree hotree(dictionary);
            hotree.Build(dataset, client);
            client = hotree.getClient();

            // 选取查询
            vector<DataRecord> sampled_queries;
            if(ds.name == "synthetic1") {
                 sampled_queries = readDataFromDataset("../../dataset/synthetic/query.txt", 50);
            } else {
                 sampled_queries = readDataFromDataset(ds.data_path, n);
            }
            
            // 随机打乱
            std::shuffle(sampled_queries.begin(), sampled_queries.end(), gen);

            double total_time = 0;
            long long total_rounds = 0;
            long long total_volume = 0;
            long long total_counter_access = 0;
            long long total_counter_self_healing_acces = 0;
            int actual_queries = min((int)sampled_queries.size(), NUM_QUERIES);

            // 执行查询
            for (int i = 0; i < actual_queries; ++i) {
                const auto& q = sampled_queries[i];

                int start_rounds = client->communication_round_trip_;
                int start_volume = client->communication_volume_;
                int start_counter_access_ = client->counter_access_;
                int start_counter_self_healing_access = client->counter_self_healing_access_;

                auto start_t = high_resolution_clock::now();
                hotree.SearchTopK(q.x_coord, q.y_coord, q.processed_text, FIXED_K, client);
                auto end_t = high_resolution_clock::now();
                
                total_time += duration_cast<microseconds>(end_t - start_t).count() / 1000.0;

                total_rounds += (client->communication_round_trip_ - start_rounds);
                total_volume += (client->communication_volume_ - start_volume);
                total_counter_access += (client->counter_access_ - start_counter_access_);
                total_counter_self_healing_acces += (client->counter_self_healing_access_ - start_counter_self_healing_access);
            }

            // 计算平均值
            double avg_t = total_time / actual_queries;
            double avg_r = (double)total_rounds / actual_queries;
            double avg_v = (double)total_volume / actual_queries;
            double avg_a = (double)total_counter_access / actual_queries;
            double avg_as = (double)total_counter_self_healing_acces / actual_queries;

            // 写入 CSV (立即写入，无需等待)
            csv << "HOTREE,"
                << ds.name << "," 
                << n << "," 
                << FIXED_K << "," 
                << avg_t << "," 
                << avg_r << "," 
                << avg_v << "," 
                << BlockSize << "," 
                << avg_a << "," 
                << avg_as << "\n";
            
            // 打印进度
            cout << "  [N=" << setw(5) << n << "] Time: " << fixed << setprecision(2) << avg_t << "ms" << endl;
        }
    }

    csv.close();
    cout << "所有测试完成，结果已保存至 " << result_filename << endl;
    return 0;
}