#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>

// 引入你的 ORAM 核心库
#include "ohash_base.hpp"
#include "ocuckoo_hash.hpp" // 包含你上面提供的 OCuckooHash 类定义
#include "types.hpp"
#include "obipartite_matching.hpp"
#include "oshuffle.hpp"
#include "prf.hpp"
#include "osort.hpp"
#include "ocompact.hpp"
#include "oblivious_operations.hpp"
#include "ohash_base.hpp"

using namespace std;
using namespace ORAM;

// 定义测试的 KeyType 和 BlockSize
using KeyType = uint32_t;
const size_t BLOCK_SIZE = 16; // 假设块大小为 16 字节

int main() {
    // 测试规模
    std::vector<KeyType> test_sizes = {512, 1024, 2048};
    
    // 失败概率控制：40 代表 2^{-40} 的失败概率 (密码学安全级别)
    // const KeyType DELTA_INV_LOG2 = 40; 

    cout << "=====================================================================" << endl;
    cout << " OCuckooHash Performance & Correctness Test (Oblivious)" << endl;
    cout << "=====================================================================" << endl;
    cout << left << setw(8) << "N" << setw(10) << "PRF_Cnt" 
         << setw(15) << "Build (ms)" << setw(15) << "Lookup (us)" 
         << "Correctness" << endl;
    cout << string(69, '-') << endl;

    for (KeyType N : test_sizes) {
        // 1. 实例化哈希表
        OCuckooHash<KeyType, BLOCK_SIZE> hash_table(N, 40);

        // 获取底层的 prf_cnt (通过 bucket_size 逆推或假设已知)
        // 在 DELTA=40 且 N=512~2048 时，你代码里的 compute_prf_cnt 通常会分配 4 个 PRF
        
        // 2. 准备真实测试数据
        vector<Block<KeyType, BLOCK_SIZE>> input_data(N);
        for (KeyType i = 0; i < N; ++i) {
            // 假设 Block 结构有 id 字段，且 id 也是我们要查找的 Key
            input_data[i].id = i + 1; // 避免使用 0，如果 0 被定义为 dummy
            // 填充一些 Payload
            // input_data[i].data[0] = ...; 
        }

        // 3. 测试构建时间 (Build)
        auto t1 = chrono::high_resolution_clock::now();
        
        hash_table.build(input_data.data());
        
        auto t2 = chrono::high_resolution_clock::now();
        double build_time = chrono::duration<double, milli>(t2 - t1).count();

        // 4. 测试查找时间与正确性 (Lookup)
        int success_cnt = 0;
        auto t3 = chrono::high_resolution_clock::now();
        
        for (KeyType i = 0; i < N; ++i) {
            KeyType target_id = input_data[i].id;
            
            // 执行不经意查找
            Block<KeyType, BLOCK_SIZE> res = hash_table[target_id];
            
            // 验证：查找结果的 ID 必须与预期一致，且不能是 Dummy
            if (res.id == target_id && !res.dummy()) {
                success_cnt++;
            }
            
            // 注意：由于 OCuckooHash 的 operator[] 会执行破坏性读取
            // (将最高位置 1)，因此第二次查找相同的 Key 会失效。
            // 这里我们只验证第一次查找的正确性。
        }
        
        auto t4 = chrono::high_resolution_clock::now();
        double lookup_time_us = chrono::duration<double, micro>(t4 - t3).count() / N;

        // 5. 输出结果
        cout << left << setw(8) << N 
             << setw(10) << "Auto" // prf_cnt 是私有的，这里标为 Auto
             << fixed << setprecision(3) << setw(15) << build_time 
             << setw(15) << lookup_time_us 
             << (success_cnt == N ? "PASS (100%)" : "FAILED") << endl;
    }

    cout << "=====================================================================" << endl;
    return 0;
}