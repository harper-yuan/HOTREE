#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <random>
#include <algorithm>

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
int two_power = 20;
const int FIXED_N = (int)pow(2,two_power); 
const vector<int> K_VALUES = {1, 2, 3, 4, 5, 6};

vector<DatasetConfig> datasets = {
    {"yelp", "../../dataset/yelp/keywords_dict.txt", "../../dataset/yelp/dataset.txt"},
    {"tweets", "../../dataset/tweets/keywords_dict.txt", "../../dataset/tweets/dataset.txt"},
    {"foursquare", "../../dataset/foursquare/keywords_dict.txt", "../../dataset/foursquare/dataset.txt"}
};

int main() {
    string result_filename = "../exp_result/obir_query_vary_K.csv";
    ofstream csv(result_filename);
    csv << "Scheme,Dataset,N,K,AvgTime_ms,AvgRounds,AvgVolume_Bytes,BlockSize,InitialTime_s\n";

    std::random_device rd;
    std::mt19937 gen(rd());

    for (const auto& ds : datasets) {
        cout << "\n>>> 正在测试数据集 (OBIRTree K变动): " << ds.name << endl;
        
        // Load dictionary once per dataset
        vector<string> dictionary = LoadDictionary(ds.dict_path);
        if (dictionary.empty()) {
            cerr << "Warning: can't load dictionary " << ds.dict_path << endl;
            continue;
        }

        // Load data once per dataset
        vector<DataRecord> data = readDataFromDataset(ds.data_path, FIXED_N);
        if (data.empty()) {
            cerr << "Warning: can't load data " << ds.data_path << endl;
            continue;
        }

        // Build OBIRTree once per dataset
        Client* client = nullptr;
        OBIRTree obirTree(dictionary);
        
        auto init_start = std::chrono::steady_clock::now();
        obirTree.Build(data, client);
        auto init_end = std::chrono::steady_clock::now();
        
        double initial_time_s = std::chrono::duration<double>(init_end - init_start).count();
        cout << "Init: " << initial_time_s << "s" << endl;

        // Sample queries once per dataset
        const int NUM_QUERIES = 100; 
        vector<DataRecord> sampled_queries = readDataFromDataset(ds.data_path, NUM_QUERIES);
        if (sampled_queries.empty()) {
            sampled_queries = readDataFromDataset(ds.data_path, 1000); // try more if needed
        }
        if (sampled_queries.empty()) continue;
        
        int actual_queries = min((int)sampled_queries.size(), NUM_QUERIES);
        std::shuffle(sampled_queries.begin(), sampled_queries.end(), gen);
        sampled_queries.resize(actual_queries);

        for (int k : K_VALUES) {
            double total_time = 0;
            double total_rounds = 0;
            double total_volume = 0;

            for (int i = 0; i < actual_queries; ++i) {
                const auto& q = sampled_queries[i];
                double start_rounds = client->communication_round_trip_;
                double start_volume = client->communication_volume_;

                auto start_t = std::chrono::steady_clock::now();
                obirTree.SearchTopK(q.x_coord, q.y_coord, q.processed_text, k, client);
                auto end_t = std::chrono::steady_clock::now();

                total_time += std::chrono::duration<double, std::milli>(end_t - start_t).count();
                total_rounds += (client->communication_round_trip_ - start_rounds);
                total_volume += (client->communication_volume_ - start_volume);
            }

            double avg_t = total_time / actual_queries;
            double avg_r = total_rounds / actual_queries;
            double avg_v = total_volume / actual_queries;

            csv << "OBIRTREE," << ds.name << "," << FIXED_N << "," << k << "," << avg_t << "," << avg_r << "," << avg_v << "," << BlockSize << "," << initial_time_s << "\n";
            csv.flush(); // ensure data is saved periodically
            
            cout << "  [K=" << setw(2) << k << "] Time: " << fixed << setprecision(2) << avg_t << "ms" << endl;
        }
        
        // Client is newed in Build if it was null, so we should clean it up
        if (client) delete client;
    }
    csv.close();
    return 0;
}
