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

// 【配置】设定并行用户（线程）数量
const int NUM_USERS = 8; 
const int FIXED_K = 1;

// 数据集配置定义
struct DatasetConfig {
    string name;
    string dict_path;
    string data_path;
};

// const vector<int> N_VALUES = {(int)pow(2,10), (int)pow(2,12), (int)pow(2,14), (int)pow(2,16)};

const vector<int> N_VALUES = {(int)pow(2,14)};

// 定义四个数据集
vector<DatasetConfig> datasets = {
    {"yelp", "../dataset/yelp/keywords_dict.txt", "../dataset/yelp/dataset.txt"},
    // {"tweets", "../dataset/tweets/keywords_dict.txt", "../dataset/tweets/dataset.txt"},
    // {"foursquare", "../dataset/foursquare/keywords_dict.txt", "../dataset/foursquare/dataset.txt"}
};

// 如果这是独立程序请用 main，如果是库函数请改回 query_vary_N
int query_vary_N() {
    string result_filename = "./exp_result/query_vary_N.csv";
    ofstream csv(result_filename);
    // 修改表头，增加 QPM
    csv << "Scheme,Dataset,N,K,AvgTime_ms,AvgRounds,AvgVolume_Bytes,BlockSize,Access,Self_Access,InitialTime_s,QPM\n";

    std::random_device rd;
    std::mt19937 gen(rd());

    // 【关键步骤 1】设置 OpenMP 线程数
    omp_set_num_threads(NUM_USERS);
    cout << ">>> [Parallel Config] Users (Threads): " << NUM_USERS << endl;
    cout << ">>> 开始测试 Vary N (并行)" << endl;

    for (const auto& ds : datasets) {
        cout << "\n>>> 正在测试数据集: " << ds.name << endl;

        for (int n : N_VALUES) {
            // 计算查询数量
            const int NUM_QUERIES = (int)(n / log2(n));
            
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

            // --- 构建索引 ---
            Client* client = nullptr;
            HOTree hotree(dictionary);
            
            auto init_start = std::chrono::steady_clock::now(); 
            
            // 【关键步骤 2】Build 时传入 NUM_USERS 初始化分段 Stash
            hotree.Build(dataset, client);
            
            auto init_end = std::chrono::steady_clock::now();   
            double initial_time_s = std::chrono::duration<double>(init_end - init_start).count();
            cout << "Init Time: " << initial_time_s << " s" << endl;
            
            client = hotree.getClient();

            // --- 选取查询 ---
            vector<DataRecord> sampled_queries;
            if(ds.name == "synthetic1") {
                 sampled_queries = readDataFromDataset("../../dataset/synthetic/query.txt", 50);
            } else {
                 sampled_queries = readDataFromDataset(ds.data_path, n); // 从数据集中采样作为查询
            }
            
            int actual_queries = min((int)sampled_queries.size(), NUM_QUERIES);
            
            // 随机打乱，混合查询难度
            std::shuffle(sampled_queries.begin(), sampled_queries.end(), gen);
            
            // 清理统计变量
            hotree.clear_additional_oblivious_shuffle_time();
            
            // 记录全局计数器初始值
            long long global_start_rounds = client->communication_round_trip_;
            long long global_start_volume = client->communication_volume_;
            long long global_start_access = client->counter_access_;
            long long global_start_self_access = client->counter_self_healing_access_;

            // --- 1. 开始并行批处理计时 (Wall Clock Start) ---
            auto batch_start_t = std::chrono::steady_clock::now();

            #pragma omp parallel for schedule(static)
            for (int i = 0; i < actual_queries; ++i) {
                // 获取当前线程 ID (0 ~ 7)
                int user_id = omp_get_thread_num(); 
                const auto& q = sampled_queries[i];

                // 【关键步骤 3】执行查询，传入 user_id
                hotree.SearchTopK(q.x_coord, q.y_coord, q.processed_text, FIXED_K, client, user_id);
            }

            // --- 2. 结束并行批处理计时 (Wall Clock End) ---
            auto batch_end_t = std::chrono::steady_clock::now();

            // --- 3. 计算并修正时间 ---
            
            // a. 测得的物理时间 (Wall Time)
            double measured_wall_time_ms = std::chrono::duration<double, std::milli>(batch_end_t - batch_start_t).count();
            
            // b. 累积的 Shuffle 模拟时间 (因为是非不经意Shuffle，需要补上时间差)
            double accumulated_shuffle_delay_ms = hotree.compute_additional_oblivious_shuffle_time();

            // c. 模拟的总真实时间 = 墙钟时间 + 模拟延迟
            double simulated_total_time_ms = measured_wall_time_ms + accumulated_shuffle_delay_ms;

            // --- 4. 计算统计指标 ---
            
            // 摊销平均耗时
            double avg_t = simulated_total_time_ms / actual_queries;
            
            // 吞吐量 QPM
            double throughput_qpm = actual_queries*60 / (simulated_total_time_ms / 1000.0);

            long long total_rounds = client->communication_round_trip_ - global_start_rounds;
            long long total_volume = client->communication_volume_ - global_start_volume;
            long long total_counter_access = client->counter_access_ - global_start_access;
            long long total_counter_self_healing_acces = client->counter_self_healing_access_ - global_start_self_access;

            double avg_r = (double)total_rounds / actual_queries;
            double avg_v = (double)total_volume / actual_queries;
            double avg_a = (double)total_counter_access / actual_queries;
            double avg_as = (double)total_counter_self_healing_acces / actual_queries;

            // 写入 CSV
            csv << "HOTREE,"
                << ds.name << "," 
                << n << "," 
                << FIXED_K << "," 
                << avg_t << "," 
                << avg_r << "," 
                << avg_v << "," 
                << BlockSize << "," 
                << avg_a << "," 
                << avg_as << "," 
                << initial_time_s << ","
                << throughput_qpm << "\n";
            
            // 打印进度
            cout << "  [N=" << setw(7) << n << "] "
                 << "Avg Time: " << fixed << setprecision(2) << avg_t << " ms | "
                 << "QPM: " << fixed << setprecision(1) << throughput_qpm << " | "
                 << "Wall: " << (long)measured_wall_time_ms << " ms | "
                 << "AddDelay: " << (long)accumulated_shuffle_delay_ms << " ms" << endl;
        }
    }

    csv.close();
    cout << "所有测试完成，结果已保存至 " << result_filename << endl;
    return 0;
}