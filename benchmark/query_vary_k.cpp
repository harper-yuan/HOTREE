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

const int NUM_QUERIES = 100000;
const int FIXED_N = 1024; // 固定数据规模
const vector<int> K_VALUES = {1, 3, 5, 7};
vector<DatasetConfig> datasets = {
    {"yelp", "../../dataset/yelp/keywords_dict.txt", "../../dataset/yelp/dataset.txt"},
    {"tweets", "../../dataset/tweets/keywords_dict.txt", "../../dataset/tweets/dataset.txt"},
    {"foursquare", "../../dataset/foursquare/keywords_dict.txt", "../../dataset/foursquare/dataset.txt"},
    {"synthetic", "../../dataset/synthetic/keywords_dict.txt", "../../dataset/synthetic/dataset.txt"}
};
int main() {
    string result_filename = "../exp_result/query_vary_K.csv";
    ofstream csv(result_filename);
    csv << "Dataset,N,K,AvgTime_ms,AvgRounds,AvgVolume_Bytes,BlockSize,Access,Self_Access\n";

    std::random_device rd;
    std::mt19937 gen(rd());

    // #pragma omp parallel for
    
    for (const auto& ds : datasets) {
        cout << "\n>>> 正在测试数据集 (K变动): " << ds.name << endl;
        
        

        for (int k : K_VALUES) {
            vector<string> dictionary = LoadDictionary(ds.dict_path);
            vector<DataRecord> data = readDataFromDataset(ds.data_path, FIXED_N);
            vector<DataRecord> sampled_queries = readDataFromDataset(ds.data_path, FIXED_N);
            int actual_queries = min((int)sampled_queries.size(), NUM_QUERIES);
            if (data.empty()) continue;

            // 构建索引 (固定 N，只需构建一次)
            Client* client = nullptr;
            HOTree hotree(dictionary);
            hotree.Build(data, client);
            client = hotree.getClient();

            // 随机打乱以选择查询点
            std::shuffle(sampled_queries.begin(), sampled_queries.end(), gen);
            
            double total_time = 0;
            long long total_rounds = 0;
            long long total_volume = 0;
            long long total_counter_access =  0;
            long long total_counter_self_healing_acces = 0;

            for (int i = 0; i < actual_queries; ++i) {
                const auto& q = sampled_queries[i];
                int start_rounds = client->communication_round_trip_;
                int start_volume = client->communication_volume_;
                int start_counter_access_ = client->counter_access_;
                int start_counter_self_healing_access = client->counter_self_healing_access_;

                auto start_t = high_resolution_clock::now();
                hotree.SearchTopK(q.x_coord, q.y_coord, q.processed_text, k, client);
                auto end_t = high_resolution_clock::now();

                total_time += duration_cast<microseconds>(end_t - start_t).count() / 1000.0;
                total_rounds += (client->communication_round_trip_ - start_rounds);
                total_volume += (client->communication_volume_ - start_volume);
                total_counter_access += (client->counter_access_ - start_counter_access_);
                total_counter_self_healing_acces += (client->counter_self_healing_access_ - start_counter_self_healing_access);
            }

            double avg_t = total_time / actual_queries;
            double avg_r = (double)total_rounds / actual_queries;
            double avg_v = (double)total_volume / actual_queries;
            double avg_a = (double)total_counter_access / actual_queries;
            double avg_as = (double)total_counter_self_healing_acces / actual_queries;

            csv << ds.name << "," << FIXED_N << "," << k << "," << avg_t << "," << avg_r << "," << avg_v << "," << BlockSize <<  "," << avg_a<< "," << avg_as << "\n";
            cout << "  [K=" << setw(2) << k << "] Time: " << fixed << setprecision(2) << avg_t << "ms" << endl;
        }
    }
    csv.close();
    return 0;
}