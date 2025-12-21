#define BOOST_TEST_MODULE CuckooHashTest
#include <boost/test/included/unit_test.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <cmath>
#include <algorithm>
#include <chrono>
// 引入上面的头文件
#include <OHT.h>
#include <tree.h>
#include <DataReader.h>
#include <client.h>
#include <omp.h>
using namespace std;

void print_current_working_directory() { cout << "Current Dir: /Mock/Path" << endl; }
vector<string> LoadDictionary(string p) { return {}; }
// --- [MOCK 结束] ---
BOOST_AUTO_TEST_SUITE(hashtable_correctness_test)


BOOST_AUTO_TEST_CASE(test_cuckoo_hash_correctness) {
    std::cout << "=== Test Started ===" << std::endl;
    
    // 1. 读取数据
    // 这里的 limit 建议设大一点，比如 2000，以触发哈希冲突
    vector<DataRecord> data = readDataFromDataset("../../dataset/synthetic_dataset.txt", 2000);
    
    if (data.empty()) {
        BOOST_FAIL("Dataset is empty!");
    }

    // 检查源数据是否有重复 ID (防止数据源本身有问题)
    std::set<int> unique_ids;
    for(const auto& d : data) unique_ids.insert(d.id);
    std::cout << "Data loaded: " << data.size() << " records, Unique IDs: " << unique_ids.size() << std::endl;

    // 2. 构建
    CuckooTable cuckooTable(512, 0); // 初始小一点，测试扩容能力
    Client* client = new Client(5);
    int level_i = 0;

    int insert_count = 0;
    for (const auto& item : data) {
        Branch dataBranch;
        dataBranch.id = item.id;
        // 确保把数据复制进去了
        dataBranch.m_rect.min_Rec[0] = item.x_coord;
        dataBranch.m_rect.min_Rec[1] = item.y_coord;
        
        cuckooTable.insert(dataBranch, client);
        insert_count++;
    }

    std::cout << "Build Finished." << std::endl;
    std::cout << "Expect Size: " << unique_ids.size() << std::endl;
    std::cout << "Actual Table Size: " << cuckooTable.size() << std::endl;
    std::cout << "Table Capacity: " << cuckooTable.capacity() << std::endl;

    // 3. 验证大小
    // 如果这里失败，说明 insert 逻辑丢数据了
    BOOST_CHECK_EQUAL(cuckooTable.size(), unique_ids.size());

    // 4. 随机抽样验证
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, data.size() - 1);
    
    int k = 100;
    std::cout << "Verifying " << k << " random elements..." << std::endl;
    
    for (int i = 0; i < k; ++i) {
        int idx = dist(gen);
        DataRecord target = data[idx];

        Branch* result = cuckooTable.find(target.id, client);
        
        if (result == nullptr) {
            // 打印详细错误信息
            std::cout << "ERROR: Failed to find ID " << target.id << std::endl;
            BOOST_ERROR("Lookup failed for existing ID");
        } else {
            // 验证 ID 是否匹配
            if (result->id != target.id) {
                 std::cout << "ERROR: ID Mismatch! Looking for " << target.id << " but found " << result->id << std::endl;
                 BOOST_ERROR("Content mismatch");
            }
        }
    }

    std::cout << "=== Test Finished ===" << std::endl;
}

// BOOST_AUTO_TEST_CASE(test_cuckoo_hash_performance) {
//     std::cout << "\n=== Cuckoo Hash Performance Benchmark ===" << std::endl;

//     // 1. 准备大规模数据
//     // 建议至少测试 10万~100万级的数据，才能看出缓存命中率的影响
//     size_t data_size = pow(2, 20); // 约 26万数据
//     vector<DataRecord> data = readDataFromDataset("../../dataset/synthetic_dataset.txt", data_size);
//     // 2. 构建哈希表
//     CuckooTable cuckooTable(data_size * 1.5, 0); // 给够初始空间，减少构建时的 rehash 干扰
//     Client* client = new Client(5);
//     int level_i = 0;
    
//     for (const auto& item : data) {
//         Branch dataBranch;
//         dataBranch.id = item.id;
//         // 简单赋值，构建不是本次测试重点
//         cuckooTable.insert(dataBranch, client);
//     }
//     std::cout << "Table Built. Size: " << cuckooTable.size() << std::endl;

//     // 3. 准备查询负载 (Pre-generate Queries)
//     // 关键步骤：在计时开始前，先把要查的所有 ID 生成好放在数组里。
//     // 避免把“生成随机数的时间”算进“哈希查找时间”里。
//     const size_t QUERY_COUNT = 1000000; // 执行 100 万次查询
//     std::vector<uint64_t> query_ids;
//     query_ids.reserve(QUERY_COUNT);

//     std::random_device rd;
//     std::mt19937 gen(rd());
//     std::uniform_int_distribution<int> dist(0, data.size() - 1);

//     for(size_t i = 0; i < QUERY_COUNT; ++i) {
//         // 随机挑选已存在的 ID 进行查询 (Hit Case)
//         query_ids.push_back(data[dist(gen)].id);
//     }

//     // 4. 开始计时
//     std::cout << "Running " << QUERY_COUNT << " lookups..." << std::endl;
    
//     // 这是一个防优化变量，确保编译器不会因为我们没用查找结果就把代码删了
//     volatile size_t check_sum = 0; 

//     auto start_time = std::chrono::high_resolution_clock::now();

//     #pragma omp parallel for
//     for (size_t i = 0; i < QUERY_COUNT; ++i) {
//         Branch* res = cuckooTable.find(query_ids[i], client);
        
//         // 简单的计算，欺骗编译器我们用了这个结果
//         if (res != nullptr) {
//             check_sum += res->id; 
//         }
//     }

//     auto end_time = std::chrono::high_resolution_clock::now();

//     // 5. 计算结果
//     auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
//     double total_time_ms = duration_ns / 1000000.0;
//     double avg_ns = (double)duration_ns / QUERY_COUNT;
//     double ops_sec = (QUERY_COUNT / total_time_ms) * 1000.0;

//     std::cout << "------------------------------------------------" << std::endl;
//     std::cout << "Total Time:   " << total_time_ms << " ms" << std::endl;
//     std::cout << "Latency:      " << avg_ns << " ns / query" << std::endl;
//     std::cout << "Throughput:   " << (size_t)ops_sec << " queries/sec" << std::endl; // QPS
//     std::cout << "------------------------------------------------" << std::endl;

//     // 只有非零才算成功（实际上肯定非零，因为都是存在的 ID）
//     BOOST_CHECK(check_sum > 0);
// }

// 辅助函数：获取当前时间戳（毫秒）
long long current_time_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

BOOST_AUTO_TEST_CASE(test_oblivious_shuffle_correctness_and_perf) {
    std::cout << "\n=== Test Started: Oblivious Shuffle Correctness & Performance ===" << std::endl;

    // 1. 准备数据
    // 建议加载足够多的数据以测试性能，例如 4096 条
    int data_limit = 5000; 
    std::vector<DataRecord> data = readDataFromDataset("../../dataset/synthetic_dataset.txt", data_limit);
    
    if (data.empty()) {
        BOOST_FAIL("Dataset is empty! Check file path.");
    }
    std::cout << "Data loaded: " << data.size() << " records." << std::endl;

    // 去重 ID，作为 Ground Truth (真值)
    std::set<int> unique_ids;
    for(const auto& d : data) unique_ids.insert(d.id);

    // 2. 初始化 Table 和 Client
    // 初始化大小设为 data.size() 附近，强制其扩容或保持紧凑
    CuckooTable cuckooTable(data.size(), 0); 
    Client* client = new Client(5);
    int level_i = 0;

    // 3. 插入数据
    for (const auto& item : data) {
        Branch dataBranch;
        dataBranch.id = item.id;
        dataBranch.m_rect.min_Rec[0] = item.x_coord;
        dataBranch.m_rect.min_Rec[1] = item.y_coord;
        dataBranch.level = 0; // 模拟层级
        dataBranch.is_empty_data = false; // 标记为真实数据
        cuckooTable.insert(dataBranch, client);
    }

    // 验证插入后的状态
    size_t size_before = cuckooTable.size();
    BOOST_CHECK_EQUAL(size_before, unique_ids.size());
    std::cout << "Initial Table Size: " << size_before << ", Capacity: " << cuckooTable.capacity() << std::endl;

    // 4. 执行 Oblivious Shuffle
    std::cout << "--- Starting Oblivious Shuffle ---" << std::endl;
    
    long long start_time = current_time_ms();
    
    // !!! 核心调用 !!!
    cuckooTable.oblivious_shuffle(client); //forever level 0 for testing
    
    long long end_time = current_time_ms();
    long long duration = end_time - start_time;

    // 5. 验证 Shuffle 后的正确性
    std::cout << "--- Verifying Correctness ---" << std::endl;

    // 5.1 验证总量守恒 (Shuffle 不应丢失或凭空增加真实数据)
    size_t size_after = cuckooTable.size();
    if (size_after != size_before) {
        std::cout << "ERROR: Size mismatch! Before: " << size_before << ", After: " << size_after << std::endl;
        BOOST_ERROR("Data count changed after shuffle!");
    }

    // 5.2 验证全量查找 (查找原来的每一个 ID)
    int found_count = 0;
    int fail_count = 0;

    for (int id : unique_ids) {
        Branch* res = cuckooTable.find(id, client);
        if (res != nullptr && res->id == id) {
            found_count++;
        } else {
            // 失败可能有两种原因：
            // 1. 数据丢了
            // 2. 数据在表里，但哈希种子没同步，导致 find 去错地方找了
            fail_count++;
            if (fail_count <= 5) { // 只打印前5个错误
                std::cout << "Failed to find ID: " << id << " after shuffle." << std::endl;
            }
        }
    }

    std::cout << "Lookup Result: Found " << found_count << " / " << unique_ids.size() << std::endl;
    BOOST_CHECK_EQUAL(found_count, unique_ids.size());

    // 6. 性能报告
    std::cout << "\n=== Performance Report ===" << std::endl;
    std::cout << "Data Size (N)   : " << unique_ids.size() << std::endl;
    std::cout << "Table Capacity  : " << cuckooTable.capacity() << std::endl;
    std::cout << "Shuffle Time    : " << duration << " ms" << std::endl;
    std::cout << "Comm Rounds     : " << client->communication_round_trip_ << std::endl;
    
    // 自动换算通信量单位
    double comm_vol_mb = (double)client->communication_volume_ / (1024.0 * 1024.0);
    std::cout << "Comm Volume     : " << comm_vol_mb << " MB" << std::endl;
    
    // 简单的吞吐量估算
    if (duration > 0) {
        std::cout << "Throughput      : " << (unique_ids.size() * 1000.0 / duration) << " items/sec" << std::endl;
    }

    std::cout << "=== Test Finished ===\n" << std::endl;
}
BOOST_AUTO_TEST_SUITE_END()