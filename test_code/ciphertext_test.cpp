#define BOOST_TEST_MODULE cihpertest
#include <boost/test/included/unit_test.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <map>
#include <tuple>
#include <limits>
#include <random>
#include <set> // 确保包含 set

#include <tree.h>        // 引入修复后的 tree.h
#include <hotree.h>
#include <DataReader.h> // 引入数据读取器
#include <chrono> // 新增计时库
using namespace std;

// 假设 DataReader.hpp 中有 LoadDictionary 函数声明
// 如果没有，可以在这里前向声明：
// vector<string> LoadDictionary(const string& filename);

void print_current_working_directory() {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        std::cout << "--- CWD 诊断信息 ---" << std::endl;
        std::cout << "当前程序运行路径 (CWD): " << cwd << std::endl;
        std::cout << "--- CWD 诊断结束 ---" << std::endl;
    } else {
        perror("getcwd() error");
    }
}
BOOST_AUTO_TEST_SUITE(cipher_correctness_test)

BOOST_AUTO_TEST_CASE(test1) {

    std::random_device rd;
    std::mt19937 gen(rd()); 
    std::uniform_int_distribution<int> dist(1, 100); 
    print_current_working_directory();
    // 确保路径正确
    vector<string> dictionary = LoadDictionary("../../dataset/synthetic/keywords_dict.txt");
    
    // 模拟 DataRecord 数据
    vector<DataRecord> data = readDataFromDataset("../../dataset/synthetic/dataset.txt", pow(2,10)); //1024
    
    // 查询数据
    vector<DataRecord> queries = readDataFromDataset("../../dataset/synthetic/query.txt");
    
    // 测试用例参数
    if (queries.empty()) {
        BOOST_FAIL("Query dataset is empty!");
    }

    int random_num = dist(gen) % queries.size();
    double qx = queries[random_num].x_coord;
    double qy = queries[random_num].y_coord;
    string qText = queries[random_num].processed_text;
    int k = 3;
    
    // 2. 构建 IR-Tree in plain and ciphertext
    PlainIRTree irTree(dictionary);
    irTree.Build(data);

    Client* client = nullptr;
    HOTree hotree(dictionary);
    hotree.Build(data, client);
    client = hotree.getClient();


    // 3. IR-Tree 搜索
    vector<pair<double, DataRecord>> ir_results = irTree.SearchTopK(qx, qy, qText, k);
    vector<pair<double, DataRecord>> hotree_results = hotree.SearchTopK(qx, qy, qText, k, client);

    cout << "IR-Tree Results (K=" << k << "):" << endl;
    vector<int> ir_ids;
    for (const auto& res : ir_results) {
        ir_ids.push_back(res.second.id);
        cout << "ID: " << res.second.id << ", Score: " << res.first << endl;
    }

    cout << "HO-Tree Results (K=" << k << "):" << endl;
    vector<int> ho_ids;
    for (const auto& res : hotree_results) {
        ho_ids.push_back(res.second.id);
        cout << "ID: " << res.second.id << ", Score: " << res.first << endl;
    }

    // 4. 验证 IR-Tree 和 HO-Tree 结果是否相等
    sort(ir_ids.begin(), ir_ids.end());
    sort(ho_ids.begin(), ho_ids.end());

    cout << "Verifying IR-Tree and HO-Tree results..." << endl;
    for (int i = 0; i < min({k, (int)ir_ids.size(), (int)ho_ids.size()}); ++i) {
        BOOST_TEST(ir_ids[i] == ho_ids[i]);
    }

    // 同时验证结果数量是否一致
    BOOST_TEST(ir_results.size() == hotree_results.size());
}

BOOST_AUTO_TEST_CASE(test_query_timing) {
    int query_num = 2048;
    // 1. 数据加载与树构建 (与 test1 共享相同的设置)
    std::random_device rd; 
    std::mt19937 gen(rd()); 
    
    // 假设这些文件和函数已被正确定义和包含
    vector<string> dictionary = LoadDictionary("../../dataset/yelp/keywords_dict.txt");
    vector<DataRecord> data = readDataFromDataset("../../dataset/yelp/dataset.txt", pow(2,10)); // 1024条数据
    vector<DataRecord> queries = readDataFromDataset("../../dataset/yelp/dataset.txt",query_num); // 1024条查询
    
    // 检查数据完整性
    if (data.empty() || queries.empty()) {
        BOOST_FAIL("数据加载失败或数据集为空，无法进行时间测试。");
    }

    Client* client = nullptr;
    HOTree hotree(dictionary);
    hotree.Build(data, client);
    client = hotree.getClient();

    // 2. 随机选择50个不同的查询
    std::vector<int> query_indices(queries.size());
    std::iota(query_indices.begin(), query_indices.end(), 0); // 填充0,1,2,...,queries.size()-1
    
    // 随机打乱索引
    std::shuffle(query_indices.begin(), query_indices.end(), gen);
    
    // 取前50个
    int num_queries = std::min(2048, static_cast<int>(queries.size()));
    std::vector<int> selected_indices(query_indices.begin(), query_indices.begin() + num_queries);
    
    // 3. 为每个查询选择不同的k值（可选）
    std::uniform_int_distribution<int> k_dist(1, 10); // k值范围1-10
    std::vector<int> k_values(num_queries);
    for (int i = 0; i < num_queries; ++i) {
        k_values[i] = k_dist(gen);
        // k_values[i] = 10;
    }

    // 4. 计时变量
    double total_time_ms = 0.0;
    std::vector<double> individual_times(num_queries);
    
    std::cout << "\n--- 查询时间测试 ---" << std::endl;
    std::cout << "测试查询数量: " << num_queries << std::endl;
    std::cout << "开始执行查询..." << std::endl;

    // 5. 执行所有查询并计时
    for (int i = 0; i < num_queries; ++i) {
        // std::cout<<"Search query "<<i<<std::endl;
        int idx = selected_indices[i];
        double qx = queries[idx].x_coord;
        double qy = queries[idx].y_coord;
        string qText = queries[idx].processed_text;
        int k = k_values[i];

        // 单个查询计时开始
        auto start = std::chrono::high_resolution_clock::now();

        // 执行查询
        vector<pair<double, DataRecord>> ir_results = hotree.SearchTopK(qx, qy, qText, k, client);

        // 单个查询计时结束
        auto end = std::chrono::high_resolution_clock::now();

        // 计算时间差 (单位：微秒)
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        double milliseconds = duration.count() / 1000.0;
        
        // 记录时间
        individual_times[i] = milliseconds;
        total_time_ms += milliseconds;
        
        // 验证查询结果
        if (ir_results.empty()) {
            std::cout << "警告: 查询 " << (i+1) << " 返回了空结果 (K=" << k << ")" << std::endl;
        }
        
        // 可选：输出每个查询的详细信息（调试用）
        // std::cout << "  查询 " << (i+1) << ": " << milliseconds << " ms (K=" << k << ")" << std::endl;
    }

    // 6. 计算统计数据
    double avg_time_ms = total_time_ms / num_queries;
    
    // 计算标准差和极值
    double min_time = *std::min_element(individual_times.begin(), individual_times.end());
    double max_time = *std::max_element(individual_times.begin(), individual_times.end());
    
    // 计算标准差
    double variance = 0.0;
    for (double time : individual_times) {
        double diff = time - avg_time_ms;
        variance += diff * diff;
    }
    variance /= num_queries;
    double std_dev = std::sqrt(variance);

    // 7. 输出结果
    std::cout << "\n--- 查询性能统计 ---" << std::endl;
    std::cout << "测试查询总数: " << num_queries << std::endl;
    std::cout << "平均查询时间: " << std::fixed << std::setprecision(4) << avg_time_ms << " ms" << std::endl;
    std::cout << "最快查询时间: " << std::fixed << std::setprecision(4) << min_time << " ms" << std::endl;
    std::cout << "最慢查询时间: " << std::fixed << std::setprecision(4) << max_time << " ms" << std::endl;
    std::cout << "查询速率: " << std::fixed << std::setprecision(2) 
              << (1000.0 / avg_time_ms) << " 查询/秒" << std::endl;
    
    // // 8. 断言验证
    // // 基本断言：至少应该有查询结果
    // BOOST_TEST(num_queries > 0, "没有执行任何查询测试");
    
    // // 性能断言示例（你可以根据实际情况调整阈值）
    // BOOST_TEST(avg_time_ms < 100.0, "平均查询时间超过了100ms阈值");
}
BOOST_AUTO_TEST_SUITE_END()