#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <random>
#include <algorithm>
#include <cmath>

#include "obir_tree.h"
#include "DataReader.h"
#include "define.h"

using namespace std;
using namespace std::chrono;

struct DatasetConfig {
    string name;
    string dict_path;
    string data_path;
};
const int FIXED_K = 1;
const vector<int> N_VALUES = {(int)pow(2,10), (int)pow(2,12), (int)pow(2,14), (int)pow(2,16), (int)pow(2,18), (int)pow(2,20)};

vector<DatasetConfig> datasets = {
    {"yelp", "../../dataset/yelp/keywords_dict.txt", "../../dataset/yelp/dataset.txt"},
    {"tweets", "../../dataset/tweets/keywords_dict.txt", "../../dataset/tweets/dataset.txt"},
    {"foursquare", "../../dataset/foursquare/keywords_dict.txt", "../../dataset/foursquare/dataset.txt"}
};

int main() {
    string result_filename = "../exp_result/obir_query_vary_N.csv";
    ofstream csv(result_filename);
    csv << "Scheme,Dataset,N,K,AvgTime_ms,AvgRounds,AvgVolume_Bytes,BlockSize,InitialTime_s\n";

    std::random_device rd;
    std::mt19937 gen(rd());

    for (const auto& ds : datasets) {
        cout << "\n>>> 正在测试数据集 (OBIRTree N变动): " << ds.name << endl;

        for (int n : N_VALUES) {
            const int NUM_QUERIES = (int)(n / log2(n));
            vector<string> dictionary = LoadDictionary(ds.dict_path);
            if (dictionary.empty()) continue;

            vector<DataRecord> dataset = readDataFromDataset(ds.data_path, n);
            if (dataset.empty()) continue;

            Client* client = nullptr;
            OBIRTree obirTree(dictionary);
            auto init_start = std::chrono::steady_clock::now();
            obirTree.Build(dataset, client);
            auto init_end = std::chrono::steady_clock::now();
            double initial_time_s = std::chrono::duration<double>(init_end - init_start).count();
            cout<< "Init: " << initial_time_s << "s"<<endl;

            vector<DataRecord> sampled_queries = readDataFromDataset(ds.data_path, n);
            std::shuffle(sampled_queries.begin(), sampled_queries.end(), gen);
            int actual_queries = min((int)sampled_queries.size(), NUM_QUERIES);

            double total_time = 0;
            double total_rounds = 0;
            double total_volume = 0;

            for (int i = 0; i < actual_queries; ++i) {
                const auto& q = sampled_queries[i];
                double start_rounds = client->communication_round_trip_;
                double start_volume = client->communication_volume_;

                auto start_t = high_resolution_clock::now();
                obirTree.SearchTopK(q.x_coord, q.y_coord, q.processed_text, FIXED_K, client);
                auto end_t = high_resolution_clock::now();
                
                total_time += duration_cast<microseconds>(end_t - start_t).count() / 1000.0;
                total_rounds += (client->communication_round_trip_ - start_rounds);
                total_volume += (client->communication_volume_ - start_volume);
            }

            double avg_t = total_time / actual_queries;
            double avg_r = total_rounds / actual_queries;
            double avg_v = total_volume / actual_queries;

            csv << "OBIRTREE," << ds.name << "," << n << "," << FIXED_K << "," << avg_t << "," << avg_r << "," << avg_v << "," << BlockSize << "," << initial_time_s << "\n";
            
            cout << "  [N=" << setw(5) << n << "] Time: " << fixed << setprecision(2) << avg_t << "ms" << endl;
        }
    }

    csv.close();
    return 0;
}
