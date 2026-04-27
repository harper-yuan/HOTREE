#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <random>

#include "../src/tree.h"
#include "../src/hotree.h"
#include "../src/DataReader.h"
#include "../src/define.h"

using namespace std;
using namespace std::chrono;

struct DatasetConfig {
    string name;
    string dict_path;
    string data_path;
};

const int TEST_N = 16384; // 与 query_vary_k 保持一致
const int NUM_QUERIES = 1500; 
const int FIXED_K = 1;        

int main() {
    string result_file = "../exp_result/query_vary_BS.csv";
    
    ifstream check_file(result_file);
    bool file_exists = check_file.good();
    check_file.close();

    ofstream csv(result_file, ios::app);
    if (!file_exists) {
        csv << "Scheme,Dataset,N,K,AvgTime_ms,AvgRounds,AvgVolume_Bytes,BlockSize,Access,Self_Access\n";
    }

    vector<DatasetConfig> datasets = {
        {"yelp", "../dataset/yelp/keywords_dict.txt", "../dataset/yelp/dataset.txt"},
        // {"tweets", "../dataset/tweets/keywords_dict.txt", "../dataset/tweets/dataset.txt"},
        // {"foursquare", "../dataset/foursquare/keywords_dict.txt", "../dataset/foursquare/dataset.txt"},
    };

    std::random_device rd;
    std::mt19937 gen(rd());

    cout << ">>> Running Query Test | BlockSize = " << BlockSize << endl;

    for (const auto& ds : datasets) {
        vector<string> dictionary = LoadDictionary(ds.dict_path);
        vector<DataRecord> data = readDataFromDataset(ds.data_path, TEST_N);
        if (data.empty()) continue;

        Client* client = nullptr;
        HOTree hotree(dictionary);
        hotree.Build(data, client);
        client = hotree.getClient();

        vector<DataRecord> sampled_queries = readDataFromDataset(ds.data_path, TEST_N);
        std::shuffle(sampled_queries.begin(), sampled_queries.end(), gen);
        int actual_queries = min((int)sampled_queries.size(), NUM_QUERIES);

        double total_time = 0;
        long long total_rounds = 0;
        long long total_volume = 0;
        long long total_access = 0;
        long long total_self_access = 0;

        for (int i = 0; i < actual_queries; ++i) {
            const auto& q = sampled_queries[i];
            int start_r = client->communication_round_trip_;
            int start_v = client->communication_volume_;
            int start_a = client->counter_access_;
            int start_sa = client->counter_self_healing_access_;

            auto start_t = high_resolution_clock::now();
            hotree.SearchTopK(q.x_coord, q.y_coord, q.processed_text, FIXED_K, client);
            auto end_t = high_resolution_clock::now();
            
            total_time += duration_cast<microseconds>(end_t - start_t).count() / 1000.0;
            total_rounds += (client->communication_round_trip_ - start_r);
            total_volume += (client->communication_volume_ - start_v);
            total_access += (client->counter_access_ - start_a);
            total_self_access += (client->counter_self_healing_access_ - start_sa);
        }

        double avg_t = total_time / actual_queries;
        double avg_r = (double)total_rounds / actual_queries;
        double avg_v = (double)total_volume / actual_queries;
        double avg_a = (double)total_access / actual_queries;
        double avg_as = (double)total_self_access / actual_queries;

        csv << "HOTREE," << ds.name << "," << TEST_N << "," << FIXED_K << "," << avg_t << "," 
            << avg_r << "," << avg_v << "," << BlockSize << "," << avg_a << "," << avg_as << "\n";
        
        cout << "  [" << ds.name << "] Time: " << fixed << setprecision(2) << avg_t << "ms" << endl;
    }
    csv.close();
    return 0;
}