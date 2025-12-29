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

// 重复次数，为了获取更稳定的初始化平均时间
const int NUM_REPEATS = 1; 

// N 值范围：可以根据需要调整，例如从 10^4 到 10^6
const vector<int> N_VALUES = {1024, (int)pow(2,12), (int)pow(2,14), (int)pow(2,16), (int)pow(2,18), (int)pow(2,20)};
// const vector<int> N_VALUES = {(int)pow(2,20)};

// 定义数据集
vector<DatasetConfig> datasets = {
    {"yelp",       "../../dataset/yelp/keywords_dict.txt",       "../../dataset/yelp/dataset.txt"},
    {"tweets",     "../../dataset/tweets/keywords_dict.txt",     "../../dataset/tweets/dataset.txt"},
    {"foursquare", "../../dataset/foursquare/keywords_dict.txt", "../../dataset/foursquare/dataset.txt"},
    {"synthetic",  "../../dataset/synthetic/keywords_dict.txt",  "../../dataset/synthetic/dataset.txt"}
};

int main() {
    string result_filename = "../exp_result/build_vary_N.csv";
    ofstream csv(result_filename);
    // 表头：数据集名称, N, 平均构建时间(ms), 通信量
    csv << "Scheme,Dataset,N,AvgBuildTime_ms,BuildVolume_Bytes,BlockSize\n";

    cout << ">>> 开始测试 HOTree 初始化时间 (Vary N)" << endl;

    for (const auto& ds : datasets) {
        cout << "\n>>> 正在处理数据集: " << ds.name << endl;

        for (int n : N_VALUES) {
            // 加载字典
            vector<string> dictionary = LoadDictionary(ds.dict_path);
            if (dictionary.empty()) {
                cerr << "Warning: 无法加载字典 " << ds.dict_path << endl;
                continue;
            }

            // 加载 N 条原始数据记录
            vector<DataRecord> dataset = readDataFromDataset(ds.data_path, n);
            if (dataset.empty() || dataset.size() < (size_t)n) {
                cout<<"dataset size "<<dataset.size()<<endl;
                cout << "  [N=" << n << "] 跳过：数据不足" << endl;
                continue;
            }

            double total_build_time = 0;
            long long last_volume = 0;

            for (int r = 0; r < NUM_REPEATS; ++r) {
                Client* client = nullptr;
                HOTree hotree(dictionary);

                // 计时开始
                auto start_t = high_resolution_clock::now();
                hotree.Build(dataset, client); 
                auto end_t = high_resolution_clock::now();

                total_build_time += duration_cast<microseconds>(end_t - start_t).count() / 1000.0;
                
                client = hotree.getClient();
                if (client) {
                    last_volume = client->communication_volume_;
                }
            }

            // 计算平均构建时间
            double avg_build_t = total_build_time / NUM_REPEATS;

            // 写入 CSV
            csv << "HOTREE,"
                <<ds.name << "," 
                << n << "," 
                << avg_build_t << "," 
                << last_volume << ","<< BlockSize<<"\n";
            
            cout << "  [N=" << setw(7) << n << "] Avg Build Time: " << fixed << setprecision(2) << avg_build_t << " ms" << endl;
        }
    }

    csv.close();
    cout << "\n所有初始化测试完成，结果已保存至 " << result_filename << endl;
    return 0;
}