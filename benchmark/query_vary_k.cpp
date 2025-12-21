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

const int NUM_QUERIES = 2;
vector<int> k_values = {1, 2, 3};
string filename = "../exp_result/query_vary_k.csv";
int n = 1024; // number of data

int main() {
    vector<string> dictionary = LoadDictionary("../../dataset/keywords_dict.txt");
    vector<DataRecord> all_queries = readDataFromDataset("../../dataset/query.txt");
    
    ofstream csv(filename);
    csv << "N,K,AvgTime_ms,AvgRounds,AvgVolume_Bytes,BlockSize\n";

    cout << "--- 执行实验 B (固定 N=" << n << ", BlockSize=" << BlockSize << ") ---" << endl;

    vector<DataRecord> data = readDataFromDataset("../../dataset/synthetic_dataset.txt", n);
    Client* client = nullptr;
    HOTree hotree(dictionary);
    hotree.Build(data, client);
    client = hotree.getClient();

    for (int k : k_values) {
        double total_time = 0;
        long long total_rounds = 0;
        long long total_volume = 0;

        for (int i = 0; i < NUM_QUERIES; ++i) {
            const auto& q = all_queries[i % all_queries.size()];

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
        cout << "K=" << setw(3) << k << " | Time: " << fixed << setprecision(3) << avg_t 
             << "ms | Rounds: " << avg_r << " | Vol: " << avg_v << " Bytes" << endl;
    }
    csv.close();
    return 0;
}