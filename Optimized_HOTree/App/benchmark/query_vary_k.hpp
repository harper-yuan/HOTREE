#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <chrono>
#include <iomanip>
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
const int NUM_USERS_THREAD = num_users; 

struct DatasetConfig {
    string name;
    string dict_path;
    string data_path;
};

int two_power = 14;
const int FIXED_N = (int)pow(2, two_power); // 固定数据规模
const vector<int> K_VALUES = {1, 2, 3, 4, 5, 6};

vector<DatasetConfig> datasets = {
    // {"tweets", "../dataset/tweets/keywords_dict.txt", "../dataset/tweets/dataset.txt"},
    {"yelp", "../dataset/yelp/keywords_dict.txt", "../dataset/yelp/dataset.txt"},
    // {"foursquare", "../dataset/foursquare/keywords_dict.txt", "../dataset/foursquare/dataset.txt"},
};

// 如果这是独立可执行文件，建议使用 main；如果是被其他文件调用，请改回 query_vary_k
int query_vary_k() {
    string result_filename = "./exp_result/query_vary_K.csv";
    ofstream csv(result_filename);
    
    // 修改表头，增加 InitialTime_s 和 QPS
    csv << "Scheme,Dataset,N,K,AvgTime_ms,AvgRounds,AvgVolume_Bytes,BlockSize,Access,Self_Access,InitialTime_s,QPS\n";

    std::random_device rd;
    std::mt19937 gen(rd());

    // 【关键步骤 1】设置 OpenMP 线程数
    omp_set_num_threads(NUM_USERS_THREAD);
    cout << ">>> [Parallel Config] Num Users (Threads): " << NUM_USERS_THREAD << endl;

    for (const auto& ds : datasets) {
        cout << "\n>>> Testing Dataset: " << ds.name << endl;
    
        for (int k : K_VALUES) {
            const int NUM_QUERIES = (int)(pow(2, two_power) / (two_power * k)); // 动态调整查询量
            
            // 读取并处理查询数据
            vector<DataRecord> sampled_queries = readDataFromDataset(ds.data_path, NUM_QUERIES);
            int actual_queries = min((int)sampled_queries.size(), NUM_QUERIES);
            
            // 随机打乱，确保每个线程分到的查询任务难度是混合的
            // std::shuffle(sampled_queries.begin(), sampled_queries.end(), gen);

            vector<string> dictionary = LoadDictionary(ds.dict_path);
            vector<DataRecord> data = readDataFromDataset(ds.data_path, FIXED_N);
            
            if (data.empty()) {
                cout << "Error: Dataset empty!" << endl;
                continue;
            }

            // --- 构建阶段 ---
            Client* client = nullptr;
            HOTree hotree(dictionary);
            
            auto init_start = std::chrono::steady_clock::now();
            
            // 【关键步骤 2】传入 NUM_USERS_THREAD 初始化分段 Stash
            hotree.Build(data, client);
            
            auto init_end = std::chrono::steady_clock::now();
            double initial_time_s = std::chrono::duration<double>(init_end - init_start).count();
            cout << "Init Time: " << initial_time_s << " s" << endl;
            
            // 获取构建后的 client 指针
            client = hotree.getClient(); 

            // --- 查询测试阶段 ---
            hotree.clear_additional_oblivious_shuffle_time();
            
            // 记录 Client 全局计数器的初始状态（用于计算增量）
            long long global_start_rounds = client->communication_round_trip_;
            long long global_start_volume = client->communication_volume_;
            long long global_start_access = client->counter_access_;
            long long global_start_self_access = client->counter_self_healing_access_;

            // 【关键步骤 3】记录批处理开始时间 (Wall Clock Start)
            auto batch_start_t = std::chrono::steady_clock::now(); 

            // 并行执行查询
            // schedule(static): 任务被均匀切分给 8 个线程，无需动态调度的开销
            #pragma omp parallel for schedule(static)
            for (int i = 0; i < actual_queries; ++i) {
                // 获取当前线程 ID (0 ~ 7) 作为 user_id
                // int user_id = omp_get_thread_num(); 
                const auto& q = sampled_queries[i];

                // 执行查询 (所有耗时都在这里，包括等待锁的时间)
                hotree.SearchTopK(q.x_coord, q.y_coord, q.processed_text, k, client, i % num_users);
            }

            // 【关键步骤 4】记录批处理结束时间 (Wall Clock End)
            auto batch_end_t = std::chrono::steady_clock::now(); 

            // 计算总墙钟时间 (Total Wall Time)
            double total_wall_time_ms = std::chrono::duration<double, std::milli>(batch_end_t - batch_start_t).count();//+hotree.compute_additional_oblivious_shuffle_time();
            
            // 计算摊销平均耗时 (Amortized Time per Query)
            // 注意：这里不需要再加 oblivious_shuffle_time，因为如果发生了驱逐，它已经包含在 wall_time 里了
            double avg_t = total_wall_time_ms / actual_queries;

            // 计算吞吐量 (QPS)
            double throughput_qpm = actual_queries*60 / (total_wall_time_ms / 1000.0);

            // 计算 Client 计数器增量
            long long total_rounds = client->communication_round_trip_ - global_start_rounds;
            long long total_volume = client->communication_volume_ - global_start_volume;
            long long total_counter_access = client->counter_access_ - global_start_access;
            long long total_counter_self_healing_acces = client->counter_self_healing_access_ - global_start_self_access;

            double avg_r = (double)total_rounds / actual_queries;
            double avg_v = (double)total_volume / actual_queries;
            double avg_a = (double)total_counter_access / actual_queries;
            double avg_as = (double)total_counter_self_healing_acces / actual_queries;

            // 写入 CSV
            csv << "HOTREE," << ds.name << "," << FIXED_N << "," << k << "," 
                << avg_t << "," << avg_r << "," << avg_v << "," << BlockSize << "," 
                << avg_a << "," << avg_as << "," << initial_time_s << "," << throughput_qpm << "\n";
            
            // 打印控制台信息
            cout << "  [K=" << setw(2) << k << "] "
                 << "Amortized: " << fixed << setprecision(3) << avg_t << " ms | "
                 << "QPS: " << fixed << setprecision(1) << throughput_qpm << " | "
                 << "WallTime: " << (long)total_wall_time_ms << " ms | "
                 << "ShuffleTime(incl): " << hotree.compute_additional_oblivious_shuffle_time() << " ms" << endl;
        }
    }
    csv.close();
    cout << ">>> Experiment Finished. Results saved to " << result_filename << endl;
    return 0;
}