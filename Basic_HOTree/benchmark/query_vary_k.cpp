#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <random>
#include <algorithm>

#include "tree.h"
#include "hotree.h"
#include "DataReader.h"
#include "define.h"

using namespace std;
using namespace std::chrono;

struct DatasetConfig {
    string name;
    string dict_path;
    string data_path;
};
int two_power = 14;
const int FIXED_N = (int)pow(2,two_power); // 固定数据规模
const vector<int> K_VALUES = {1, 2, 3, 4, 5, 6};

vector<DatasetConfig> datasets = {
    {"yelp", "../../dataset/yelp/keywords_dict.txt", "../../dataset/yelp/dataset.txt"},
    // {"tweets", "../../dataset/tweets/keywords_dict.txt", "../../dataset/tweets/dataset.txt"},
    // {"foursquare", "../../dataset/foursquare/keywords_dict.txt", "../../dataset/foursquare/dataset.txt"}
};

int main() {
    string result_filename = "../exp_result/query_vary_K.csv";
    ofstream csv(result_filename);
    // 修改表头，增加 InitialTime_s 列
    csv << "Scheme,Dataset,N,K,AvgTime_ms,AvgRounds,AvgVolume_Bytes,BlockSize,Access,Self_Access,InitialTime_s\n";

    std::random_device rd;
    std::mt19937 gen(rd());

    for (const auto& ds : datasets) {
        cout << "\n>>> 正在测试数据集 (K变动): " << ds.name << endl;
    
        for (int k : K_VALUES) {
            const int NUM_QUERIES = (int)(pow(2, two_power) / (two_power*k)); // 2^18
            vector<DataRecord> sampled_queries = readDataFromDataset(ds.data_path, NUM_QUERIES);
            int actual_queries = min((int)sampled_queries.size(), NUM_QUERIES);
            // 随机打乱以选择查询点
            std::shuffle(sampled_queries.begin(), sampled_queries.end(), gen);


            vector<string> dictionary = LoadDictionary(ds.dict_path);
            vector<DataRecord> data = readDataFromDataset(ds.data_path, FIXED_N);
            
            
            if (data.empty()) continue;

            // --- 记录初始化时间开始 ---
            Client* client = nullptr;
            HOTree hotree(dictionary);
            
            auto init_start = std::chrono::steady_clock::now(); // 开始计时
            hotree.Build(data, client);
            auto init_end = std::chrono::steady_clock::now();   // 结束计时
            
            
            // 计算初始化时间（秒）
            double initial_time_s = std::chrono::duration<double>(init_end - init_start).count();
            cout<< "Init: " << initial_time_s << "s"<<endl;
            
            client = hotree.getClient();
            // --- 记录初始化时间结束 ---

        
            
            hotree.clear_additional_oblivious_shuffle_time();
            double total_time = 0;
            long long total_rounds = 0;
            long long total_volume = 0;
            long long total_counter_access =  0;
            long long total_counter_self_healing_acces = 0;

            // Since each query leads to multiple accesses, this is sufficient to ensure at least N accesses
            for (int i = 0; i < actual_queries; ++i) {
                const auto& q = sampled_queries[i];
                double start_rounds = client->communication_round_trip_;
                double start_volume = client->communication_volume_;
                int start_counter_access_ = client->counter_access_;
                int start_counter_self_healing_access = client->counter_self_healing_access_;

                auto start_t = std::chrono::steady_clock::now();
                hotree.SearchTopK(q.x_coord, q.y_coord, q.processed_text, k, client);
                auto end_t = std::chrono::steady_clock::now();

                total_time += std::chrono::duration<double, std::milli>(end_t - start_t).count();
                total_rounds += (client->communication_round_trip_ - start_rounds);
                total_volume += (client->communication_volume_ - start_volume);

                total_counter_access += (client->counter_access_ - start_counter_access_);
                total_counter_self_healing_acces += (client->counter_self_healing_access_ - start_counter_self_healing_access);
            }

            double avg_t = (total_time + hotree.compute_additional_oblivious_shuffle_time()) / actual_queries;
            double avg_r = (double)total_rounds / actual_queries;
            double avg_v = (double)total_volume / actual_queries;
            double avg_a = (double)total_counter_access / actual_queries;
            double avg_as = (double)total_counter_self_healing_acces / actual_queries;

            // 在 CSV 写入行末尾增加 initial_time_s
            csv << "HOTREE," << ds.name << "," << FIXED_N << "," << k << "," << avg_t << "," << avg_r << "," << avg_v << "," << BlockSize << "," << avg_a << "," << avg_as << "," << initial_time_s << "\n";
            
            cout << "  [K=" << setw(2) << k << "] Time: " << fixed << setprecision(2) << avg_t << "ms" << " oblivious shuffle time: "<< hotree.compute_additional_oblivious_shuffle_time() << "ms" << endl;
        }
    }
    csv.close();
    return 0;
}