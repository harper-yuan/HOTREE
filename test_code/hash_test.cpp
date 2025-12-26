#define BOOST_TEST_MODULE CuckooHashTest
#include <boost/test/included/unit_test.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <set>

#include <OHT.h>
#include <tree.h>
#include <DataReader.h>
#include <client.h>
#include <omp.h>

using namespace std;

// --- Mock 函数保持不变 ---
void print_current_working_directory() { cout << "Current Dir: /Mock/Path" << endl; }
vector<string> LoadDictionary(string p) { return {}; }

BOOST_AUTO_TEST_SUITE(hashtable_correctness_test)

BOOST_AUTO_TEST_CASE(test_cuckoo_hash_correctness) {
    std::cout << "=== Test Started: Basic Correctness ===" << std::endl;
    
    // 1. 读取数据
    int N = 2000;
    // 注意：请确保路径与你的环境一致
    vector<DataRecord> data = readDataFromDataset("../../dataset/synthetic/dataset.txt", N);
    
    if (data.empty()) {
        BOOST_FAIL("Dataset is empty!");
    }

    // 检查源数据是否有重复 ID
    std::set<int> unique_ids;
    for(const auto& d : data) unique_ids.insert(d.id);
    std::cout << "Data loaded: " << data.size() << " records, Unique IDs: " << unique_ids.size() << std::endl;

    // 2. 构建
    CuckooTable cuckooTable(N, 0); 
    Client* client = new Client(5);
    
    // 用于管理堆内存生命周期
    std::vector<Branch*> branch_pool;
    branch_pool.reserve(data.size());

    for (const auto& item : data) {
        // 关键修改：必须在堆上分配，确保地址唯一且持久
        Branch* pBranch = new Branch(); 
        pBranch->id = item.id;
        pBranch->m_rect.min_Rec[0] = item.x_coord;
        pBranch->m_rect.min_Rec[1] = item.y_coord;
        
        cuckooTable.insert(pBranch, client); // 传入指针
        branch_pool.push_back(pBranch);
    }

    std::cout << "Build Finished." << std::endl;
    std::cout << "Expect Size: " << unique_ids.size() << std::endl;
    std::cout << "Actual Table Size: " << cuckooTable.size() << std::endl;

    // 3. 验证大小
    BOOST_CHECK_EQUAL(cuckooTable.size(), unique_ids.size());

    // 4. 随机抽样验证
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, data.size() - 1);
    
    int k = 100;
    for (int i = 0; i < k; ++i) {
        int idx = dist(gen);
        DataRecord target = data[idx];

        Branch* result = cuckooTable.find(target.id, 0, client);
        
        if (result == nullptr) {
            BOOST_ERROR("Lookup failed for existing ID: " << target.id);
        } else {
            BOOST_CHECK_EQUAL(result->id, target.id);
        }
    }

    // 清理内存
    for (auto p : branch_pool) delete p;
    delete client;
    std::cout << "=== Test Finished ===\n" << std::endl;
}

// BOOST_AUTO_TEST_CASE(test_cuckoo_hash_performance) {
//     std::cout << "=== Cuckoo Hash Performance Benchmark ===" << std::endl;

//     // 1. 准备大规模数据
//     size_t data_size = pow(2, 20); // 约 100 万数据
//     vector<DataRecord> data = readDataFromDataset("../../dataset/synthetic/dataset.txt", data_size);
//     if (data.empty()) BOOST_FAIL("Data loading failed.");

//     CuckooTable cuckooTable(data_size * 1.5, 0); 
//     Client* client = new Client(5);
    
//     std::vector<Branch*> branch_pool;
//     branch_pool.reserve(data.size());

//     std::cout << "Building table with " << data.size() << " elements..." << std::endl;
//     for (const auto& item : data) {
//         Branch* pBranch = new Branch();
//         pBranch->id = item.id;
//         cuckooTable.insert(pBranch, client);
//         branch_pool.push_back(pBranch);
//     }
//     std::cout << "Table Built. Size: " << cuckooTable.size() << std::endl;

//     // 2. 准备查询负载
//     const size_t QUERY_COUNT = 1000000;
//     std::vector<uint64_t> query_ids;
//     query_ids.reserve(QUERY_COUNT);

//     std::random_device rd;
//     std::mt19937 gen(rd());
//     std::uniform_int_distribution<int> dist(0, data.size() - 1);

//     for(size_t i = 0; i < QUERY_COUNT; ++i) {
//         query_ids.push_back(data[dist(gen)].id);
//     }

//     // 3. 开始计时
//     std::cout << "Running " << QUERY_COUNT << " lookups..." << std::endl;
//     volatile size_t check_sum = 0; 
//     auto start_time = std::chrono::high_resolution_clock::now();

//     #pragma omp parallel for reduction(+:check_sum)
//     for (size_t i = 0; i < QUERY_COUNT; ++i) {
//         Branch* res = cuckooTable.find(combine_unique(query_ids[i], 0), client);
//         if (res != nullptr) {
//             check_sum += res->id; 
//         }
//     }

//     auto end_time = std::chrono::high_resolution_clock::now();

//     // 4. 输出统计
//     auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
//     double total_time_ms = duration_ns / 1000000.0;
//     double avg_ns = (double)duration_ns / QUERY_COUNT;
//     double ops_sec = (QUERY_COUNT / total_time_ms) * 1000.0;

//     std::cout << "Total Time:   " << total_time_ms << " ms" << std::endl;
//     std::cout << "Latency:      " << avg_ns << " ns / query" << std::endl;
//     std::cout << "Throughput:   " << (size_t)ops_sec << " queries/sec" << std::endl;

//     BOOST_CHECK(check_sum > 0);

//     // 清理内存
//     for (auto p : branch_pool) delete p;
//     delete client;
// }

BOOST_AUTO_TEST_CASE(test_oblivious_shuffle_correctness_and_perf) {
    std::cout << "\n=== Test Started: Oblivious Shuffle Correctness & Performance ===" << std::endl;

    int data_limit = 5000; 
    std::vector<DataRecord> data = readDataFromDataset("../../dataset/synthetic/dataset.txt", data_limit);
    if (data.empty()) BOOST_FAIL("Dataset is empty!");

    std::set<int> unique_ids;
    for(const auto& d : data) unique_ids.insert(d.id);

    CuckooTable cuckooTable(data.size(), 0); 
    Client* client = new Client(5);
    std::vector<Branch*> branch_pool;

    // 1. 插入数据
    for (const auto& item : data) {
        Branch* pBranch = new Branch();
        pBranch->id = item.id;
        pBranch->m_rect.min_Rec[0] = item.x_coord;
        pBranch->m_rect.min_Rec[1] = item.y_coord;
        pBranch->level = 0;
        pBranch->is_empty_data = false;
        cuckooTable.insert(pBranch, client);
        branch_pool.push_back(pBranch);
    }

    size_t size_before = cuckooTable.size();
    std::cout << "Initial Table Size: " << size_before << std::endl;

    // 2. 执行 Oblivious Shuffle
    std::cout << "--- Starting Oblivious Shuffle ---" << std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    cuckooTable.oblivious_shuffle(client); 
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    // 3. 验证正确性
    size_t size_after = cuckooTable.size();
    BOOST_CHECK_EQUAL(size_after, size_before);

    int found_count = 0;
    for (int id : unique_ids) {
        Branch* res = cuckooTable.find(id, 0, client);
        if (res != nullptr && res->id == id) {
            found_count++;
        }
    }

    std::cout << "Lookup Result after Shuffle: Found " << found_count << " / " << unique_ids.size() << std::endl;
    BOOST_CHECK_EQUAL(found_count, unique_ids.size());

    // 4. 性能报告
    double comm_vol_mb = (double)client->communication_volume_ / (1024.0 * 1024.0);
    double comm_round_trips = (double)client->communication_round_trip_;
    std::cout << "Shuffle Time    : " << duration << " ms" << std::endl;
    std::cout << "Comm Volume     : " << comm_vol_mb << " MB" << std::endl;
    std::cout << "Comm Round     : " << comm_round_trips << std::endl;
    // 清理内存
    for (auto p : branch_pool) delete p;
    delete client;
}

BOOST_AUTO_TEST_SUITE_END()