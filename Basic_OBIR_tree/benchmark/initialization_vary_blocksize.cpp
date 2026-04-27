#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <chrono>
#include <iomanip>

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

const int FIXED_N = 16384; // 建议根据需要调大，如 (int)pow(2,16)
const int NUM_REPEATS = 1; 

vector<DatasetConfig> datasets = {
    {"yelp", "../dataset/yelp/keywords_dict.txt", "../dataset/yelp/dataset.txt"},
    // {"tweets", "../dataset/tweets/keywords_dict.txt", "../dataset/tweets/dataset.txt"},
    // {"foursquare", "../dataset/foursquare/keywords_dict.txt", "../dataset/foursquare/dataset.txt"},
};

int main() {
    string result_filename = "../exp_result/initialization_vary_BS.csv";
    
    ifstream check_file(result_filename);
    bool file_exists = check_file.good();
    check_file.close();

    ofstream csv(result_filename, ios::app);
    if (!file_exists) {
        csv << "Scheme,Dataset,N,K,AvgTime_ms,AvgRounds,AvgVolume_Bytes,BlockSize,Access,Self_Access\n";
    }

    cout << ">>> 开始测试初始化时间 (当前 BlockSize: " << BlockSize << ")" << endl;

    for (const auto& ds : datasets) {
        vector<string> dictionary = LoadDictionary(ds.dict_path);
        vector<DataRecord> dataset = readDataFromDataset(ds.data_path, FIXED_N);
        
        if (dataset.empty()) continue;

        double total_build_time = 0;
        long long total_rounds = 0;
        long long total_volume = 0;
        long long total_access = 0;
        long long total_self_access = 0;

        for (int r = 0; r < NUM_REPEATS; ++r) {
            Client* client = nullptr;
            HOTree hotree(dictionary);

            auto start_t = high_resolution_clock::now();
            hotree.Build(dataset, client);
            auto end_t = high_resolution_clock::now();

            total_build_time += duration_cast<microseconds>(end_t - start_t).count() / 1000.0;
            
            client = hotree.getClient();
            if (client) {
                total_rounds += client->communication_round_trip_;
                total_volume += client->communication_volume_;
                total_access += client->counter_access_;
                total_self_access += client->counter_self_healing_access_;
            }
        }

        double avg_t = total_build_time / NUM_REPEATS;
        double avg_r = (double)total_rounds / NUM_REPEATS;
        double avg_v = (double)total_volume / NUM_REPEATS;
        double avg_a = (double)total_access / NUM_REPEATS;
        double avg_as = (double)total_self_access / NUM_REPEATS;

        // K 填 0 表示初始化阶段
        csv << "HOTREE," << ds.name << "," << FIXED_N << ",0," << avg_t << "," 
            << avg_r << "," << avg_v << "," << BlockSize << "," << avg_a << "," << avg_as << "\n";
        
        cout << "  [" << ds.name << "] Build Time: " << fixed << setprecision(2) << avg_t << "ms" << endl;
    }

    csv.close();
    return 0;
}