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
#include <omp.h> // 引入 OpenMP

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

const int NUM_QUERIES = 30000; // 样本数
const int FIXED_K = 1;

// const vector<int> N_VALUES = {1024, 2048}; 
const vector<int> N_VALUES = {1024, (int)pow(2,11), (int)pow(2,12), (int)pow(2,13), (int)pow(2,14)};

// 定义四个数据集
vector<DatasetConfig> datasets = {
    {"yelp",       "../../dataset/yelp/keywords_dict.txt",       "../../dataset/yelp/dataset.txt"},
    {"tweets",     "../../dataset/tweets/keywords_dict.txt",     "../../dataset/tweets/dataset.txt"},
    {"foursquare", "../../dataset/foursquare/keywords_dict.txt", "../../dataset/foursquare/dataset.txt"},
    {"synthetic",  "../../dataset/synthetic/keywords_dict.txt",  "../../dataset/synthetic/dataset.txt"}
};

// 1. 定义结果结构体
struct ExperimentResult {
    string dataset_name;
    int n;
    int k;
    double avg_t;
    double avg_r;
    double avg_v;
    double avg_a;
    double avg_as;
    bool valid; // 标记该任务是否有效（如数据加载失败则为 false）
};

// 2. 定义任务结构体
struct Task {
    int dataset_idx;
    int n_val;
};

int main() {
    string result_filename = "../exp_result/query_vary_N.csv";
    
    // 3. 准备任务列表（扁平化双重循环）
    vector<Task> task_list;
    for (int i = 0; i < datasets.size(); ++i) {
        for (int n : N_VALUES) {
            task_list.push_back({i, n});
        }
    }

    // 预分配结果数组，保证索引对应
    vector<ExperimentResult> results(task_list.size());

    cout << ">>> 开始并行测试 Vary N, 总任务数: " << task_list.size() << " (线程数: " << omp_get_max_threads() << ")" << endl;

    // 4. 并行计算
    #pragma omp parallel
    for (int t = 0; t < task_list.size(); ++t) {
        // 获取任务参数
        int ds_idx = task_list[t].dataset_idx;
        int n = task_list[t].n_val;
        const auto& ds = datasets[ds_idx];

        // 线程局部变量
        std::random_device rd;
        std::mt19937 gen(rd());

        // 加载字典
        vector<string> dictionary = LoadDictionary(ds.dict_path);
        if (dictionary.empty()) {
            results[t].valid = false;
            // cerr << "Warning: can't load dictionary " << ds.dict_path << endl; // 多线程下建议少用 cerr 以免混乱
            continue;
        }

        // 加载 N 条数据
        vector<DataRecord> dataset = readDataFromDataset(ds.data_path, n);
        if (dataset.empty()) {
            results[t].valid = false;
            continue;
        }

        // 构建索引 (线程局部对象)
        Client* client = nullptr;
        HOTree hotree(dictionary);
        hotree.Build(dataset, client);
        client = hotree.getClient();

        // 选取查询
        vector<DataRecord> sampled_queries;
        if(ds.name == "synthetic1") {
             sampled_queries = readDataFromDataset("../../dataset/synthetic/query.txt", 50);
        } else {
             // 注意：这里可能会读取大量数据，如果 query 文件很大，多线程同时读可能会有 IO 压力
             // 但对于 N=16384 这种规模通常是可以接受的
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

            // 记录其实状态
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

        // 存储结果
        results[t].dataset_name = ds.name;
        results[t].n = n;
        results[t].k = FIXED_K;
        results[t].avg_t = total_time / actual_queries;
        results[t].avg_r = (double)total_rounds / actual_queries;
        results[t].avg_v = (double)total_volume / actual_queries;
        results[t].avg_a = (double)total_counter_access / actual_queries;
        results[t].avg_as = (double)total_counter_self_healing_acces / actual_queries;
        results[t].valid = true;

        // 进度提示 (可选)
        // #pragma omp critical
        cout << "Dataset: " << ds.name 
             << " [N=" << setw(2) << n << "] Time: " << fixed << setprecision(2) << total_time / actual_queries << "ms" << endl;
    }

    // 5. 顺序写入结果
    ofstream csv(result_filename);
    csv << "Dataset,N,K,AvgTime_ms,AvgRounds,AvgVolume_Bytes,BlockSize,Access,Self_Access\n";

    for (const auto& res : results) {
        if (!res.valid) continue;

        csv << res.dataset_name << "," 
            << res.n << "," 
            << res.k << "," 
            << res.avg_t << "," 
            << res.avg_r << "," 
            << res.avg_v << "," 
            << BlockSize << "," 
            << res.avg_a << "," 
            << res.avg_as << "\n";
        
        // cout << ">>> test dataset: " << res.dataset_name << " (BlockSize=" << BlockSize << ")" << endl;
        // cout << "  [N=" << setw(5) << res.n << "] Time: " << fixed << setprecision(2) << res.avg_t 
        //      << "ms | Rounds: " << res.avg_r << " | Vol: " << (long)res.avg_v << " Bytes" << endl;
    }

    csv.close();
    cout << "所有测试完成，结果已保存至 " << result_filename << endl;
    return 0;
}