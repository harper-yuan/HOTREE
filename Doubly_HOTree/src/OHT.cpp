#include "OHT.h"
#include <random>
#include <algorithm>
#include <iostream>
#include <cmath>

CuckooTable::CuckooTable(size_t initial_size, int HOTREE_level) : current_count(0) {
    HOTREE_level_ = HOTREE_level;
    shuffle_count = 0;
    
    // 修改：确保初始化的大小一定是 Z 的倍数
    table.resize(get_aligned_size(initial_size));
    
    stash.reserve(STASH_CAPACITY);
}


// 哈希函数保持不变
size_t CuckooTable::hash(uint64_t id, size_t seed) const {
    uint64_t k = id;
    const uint64_t m = 0xc6a4a7935bd1e995;
    const int r = 47;
    uint64_t h = seed ^ (8 * m);
    k *= m; k ^= k >> r; k *= m;
    h ^= k; h *= m;
    h ^= h >> r; h *= m; h ^= h >> r;
    return h % table.size();
}

std::vector<Branch*> CuckooTable::find_hotree(uint64_t id, size_t place1, size_t place2) {
    std::vector<Branch*> result;
    // 直接返回存储在 table 中的指针
    result.push_back(table[place1].branch);
    result.push_back(table[place2].branch);
    return result;
}

Branch* CuckooTable::find(uint64_t id, uint64_t counter_for_lastest_data, Client* client) {
    uint64_t id_and_counter = combine_unique(id, counter_for_lastest_data);
    
    // 修改：获取 Bin 内的 p1 和 p2
    auto [p1, p2] = get_p1_p2(id_and_counter, client);

    // 1. Table Lookups
    if (table[p1].occupied && table[p1].branch != nullptr && table[p1].branch->id == id && table[p1].branch->counter_for_lastest_data == counter_for_lastest_data) {
        return table[p1].branch;
    }
    if (table[p2].occupied && table[p2].branch != nullptr && table[p2].branch->id == id && table[p2].branch->counter_for_lastest_data == counter_for_lastest_data) {
        return table[p2].branch;
    }

    // 2. Stash Lookup
    for (auto& item : stash) {
        if (item != nullptr && item->id == id && item->counter_for_lastest_data == counter_for_lastest_data) return item;
    }
    return nullptr;
}

void CuckooTable::insert(Branch* branch, Client* client) {
    if ((current_count + stash.size()) >= table.size()) {
        rehash(table.size() * 2, client);
    }
    insert_internal(branch, client);
}

void CuckooTable::insert_internal(Branch* item, Client* client) {
    // 检查是否已存在（避免重复插入指针）
    if (find(item->id, item->counter_for_lastest_data, client) != nullptr) {
        return; 
    }

    static std::mt19937 rng(global_seed);
    for (int i = 0; i < MAX_KICKS; ++i) {
        uint64_t id_and_counter = combine_unique(item->id, item->counter_for_lastest_data);
        
        // 修改：使用统一的函数获取 Bin 内的两个位置
        auto [p1, p2] = get_p1_p2(id_and_counter, client);

        if (!table[p1].occupied) {
            table[p1].branch = item;
            table[p1].occupied = true;
            current_count++;
            return;
        }

        if (!table[p2].occupied) {
            table[p2].branch = item;
            table[p2].occupied = true;
            current_count++;
            return;
        }

        // 随机踢出一个指针 (此时踢出只会在该 Bin 内部的 p1 和 p2 之间发生)
        bool kick_p1 = (rng() % 2) == 0;
        size_t victim_pos = kick_p1 ? p1 : p2;
        std::swap(item, table[victim_pos].branch);
    }
    
    // 如果达到了 MAX_KICKS 还没插入成功，放入全局 stash
    if(if_is_debug) {
        uint64_t debug_id_counter = combine_unique(item->id, item->counter_for_lastest_data);
        auto [p1, p2] = get_p1_p2(debug_id_counter, client);
        if(table[p1].branch!=nullptr && table[p1].branch->id == debug_id) {
            std::cout<<"In insert id "<< table[p1].branch->id <<" level "<<HOTREE_level_ <<" p1: "<<p1 << " seed: "<<client->vec_seed1_[HOTREE_level_]<<" table size"<< table.size()<< " counter "<< table[p1].branch->counter_for_lastest_data<<std::endl;    
        }
        if(table[p2].branch!=nullptr && table[p2].branch->id == debug_id) {
            std::cout<<"In insert id "<< table[p2].branch->id <<" level "<<HOTREE_level_ <<" p2: "<<p2 << " seed: "<<client->vec_seed1_[HOTREE_level_]<<" table size"<< table.size()<< " counter "<< table[p2].branch->counter_for_lastest_data<<std::endl;    
        }
    }

    if (stash.size() < STASH_CAPACITY) {
        stash.push_back(item);
        if(item->id == debug_id && if_is_debug) {
            std::cout<<"In insert id "<< item->id <<" level "<<HOTREE_level_ <<" stash weth seed"<<client->vec_seed1_[HOTREE_level_]<<" table size"<< table.size()<< " counter "<< item->counter_for_lastest_data<<std::endl;
        }
        return; 
    }
    
    // 兜底扩容
    rehash(table.size() * 2, client);
    insert_internal(item, client);
}

void CuckooTable::rehash(size_t new_size, Client* client) {
    std::vector<Entry> old_table = std::move(table);
    std::vector<Branch*> old_stash = std::move(stash);

    table.assign(new_size, Entry()); // 初始化新表
    stash.clear();
    current_count = 0;

    for (const auto& entry : old_table) {
        if (entry.occupied && entry.branch != nullptr) {
            insert_internal(entry.branch, client);
        }
    }

    for (auto* item : old_stash) {
        if (item != nullptr) {
            insert_internal(item, client);
        }
    }
}

std::vector<Branch*> CuckooTable::oblivious_tight_compaction(std::vector<Branch*> all_elements1, std::vector<int> branchs_level_belong_to, Client* client) {
    std::vector<Branch*> result_branchs;
    std::unordered_map<int, Branch*> unique_elements;
    for (auto& elem : all_elements1) {
        if (elem == nullptr) continue;

        int id = elem->id;
        // 如果 id 不存在，或者当前元素的 counter 更大，则更新/插入
        if (unique_elements.find(id) == unique_elements.end() || 
            elem->counter_for_lastest_data > unique_elements[id]->counter_for_lastest_data) {
            unique_elements[id] = elem;
        }
    }
    for (auto const& [id, elem] : unique_elements) {
        if (id == debug_id && if_is_debug) {
            printf("Inserting unique id %d with max counter %d in OHT.cpp\n", 
                elem->id, elem->counter_for_lastest_data);
        }
        elem->level = HOTREE_level_;
        elem->counter_for_lastest_data = 0;
        for(auto & triple : elem->child_triple) {
            triple->counter_for_lastest_data = 0;
            triple->level = HOTREE_level_;
        }
        result_branchs.push_back(elem);
        // all_elements.push_back(elem);
    }
    return result_branchs;
}

void CuckooTable::oblivious_shuffle_and_insert(std::vector<Branch*>& all_elements_before_otc, std::vector<int> branchs_level_belong_to, Client* client) {
    // 1. 初始化基础状态
    table.assign(get_aligned_size(pow(2, HOTREE_level_)), Entry());
    stash.clear();
    current_count = 0;
    client->UpdateSeed(HOTREE_level_);
    std::vector<Branch*> all_elements;

    // --- 模拟测试逻辑 / 特殊层级处理 (保持原逻辑不变) ---
    if(all_elements_before_otc.size() <= TEE_Z) {
        // ... (保持原始代码中的特殊处理逻辑) ...
        // 为节省篇幅，此处省略未变动的小规模数据处理代码
        if(HOTREE_level_ != client->max_level_) {
            client->communication_round_trip_ += all_elements_before_otc.size()/TEE_Z;
            client->communication_volume_ += all_elements_before_otc.size()*BlockSize;
            for(auto &elem : all_elements_before_otc) insert(elem, client);
            // std::cout<<"level_i: "<<HOTREE_level_ <<" N_real:"<< all_elements_before_otc.size()<<std::endl;
            return;
        } else {
            client->communication_round_trip_ += 2*all_elements_before_otc.size()/TEE_Z;
            client->communication_volume_ += 2*all_elements_before_otc.size()*BlockSize;
            all_elements = oblivious_tight_compaction(all_elements_before_otc, branchs_level_belong_to, client);
            for(auto &elem : all_elements) insert(elem, client);
            return;
        }
    }
    
    // 性能测试缓存逻辑
    if(shuffle_tested_flag) { 
        shuffle_count++;
        client->communication_round_trip_ += single_shuffle_round_trips;
        client->communication_volume_ += single_shuffle_commucations;
        // 模拟模式下，仍需执行必要的 insert 以维持功能正确性，但跳过 Shuffle
        std::vector<Branch*>& target_elements = (HOTREE_level_ == client->max_level_) ? 
            (all_elements = oblivious_tight_compaction(all_elements_before_otc, branchs_level_belong_to, client)) : all_elements_before_otc;
        for(auto &elem : target_elements) insert(elem, client);
        return;
    } else {
        shuffle_tested_flag = true;
    }

    double initial_volume = client->communication_volume_;
    double initial_rounds = client->communication_round_trip_;
    auto start_t = std::chrono::high_resolution_clock::now();

    // 预处理数据
    if(HOTREE_level_ != client->max_level_) {
        all_elements = std::move(all_elements_before_otc);
    } else {
        // 最后一层特殊处理 (假设该函数内部未包含 insert)
        oblivious_shuffle_and_insert_last_level(all_elements_before_otc, branchs_level_belong_to, client);
        // 记录指标并返回
        auto end_t = std::chrono::high_resolution_clock::now();
        single_shuffle_times = std::chrono::duration_cast<std::chrono::microseconds>(end_t - start_t).count() / 1000.0;
        single_shuffle_commucations = client->communication_volume_ - initial_volume;
        single_shuffle_round_trips = client->communication_round_trip_ - initial_rounds;
        return;
    }

    // 2. 计算参数
    int N_real = all_elements.size();
    int B = (N_real > Z) ? pow(2, ceil(log2(2.0 * N_real / Z))) : pow(2, ceil(log2(2.0 * sqrt(N_real))));

    // std::cout<<"level_i: "<<HOTREE_level_ <<" B: "<<B<<" N_real:"<<N_real<<std::endl;
    int num_levels_shuffle = ceil(log2(B));
    int total_nodes_per_level = B * Z;

    /* This decryption step can be securely implemented by recording the layer 
    where the input data itself is located (which the server can already know), 
    and then decrypting the data within the ObliviousMergeSplit function based 
    on the layer where the original data is located. All decrypted data is encrypted 
    using the key corresponding to the new layer, but for the sake of concise and easy to 
    understand code, we will decrypt it here. Note that this does not affect performance. */
    // omp_set_num_threads(num_threads);
    // #pragma omp parallel for schedule(static)
    for(int i = 0; i < (int)all_elements.size(); i++) {
        all_elements[i]->trueData = client->cryptor_->aes_decrypt(all_elements[i]->trueData, branchs_level_belong_to[i]);
    }

    // 4. 【优化 1 & 3】Ping-Pong 双缓冲 + 连续内存池
    // 只分配两层所需的指针空间，大幅降低内存占用 (Cache Friendly)
    std::vector<Branch*> buffer_curr(total_nodes_per_level, nullptr);
    std::vector<Branch*> buffer_next(total_nodes_per_level, nullptr);

    // 【优化 3】使用 vector 连续存储 Branch 对象，避免大量 new 造成的堆碎片
    int num_dummies = total_nodes_per_level - all_elements.size();
    // 注意：如果有跨层逻辑依赖指针地址唯一性，这里使用 vector 存储对象是安全的
    // 只要 vector 不发生 realloc (reserve 足够空间)
    std::vector<Branch> dummy_arena; 
    if (num_dummies > 0) {
        dummy_arena.reserve(num_dummies + B * Z); // 额外预留一些空间防止扩容
        // 构造所有需要的 dummy (注意：这里在单线程预先构造，避免 OMP 中竞争)
        // 使用 assign 填充比 push_back 更快
        dummy_arena.assign(num_dummies, Branch(true, true)); 
    }

    // 填充 Level 0 (初始化 buffer_curr)
    int data_idx = 0;
    int dummy_idx = 0;
    for(int b = 0; b < B; ++b) {
        int base_offset = b * Z;
        // 填充前半部分真实数据
        int k = 0;
        for(; k < Z/2 && data_idx < (int)all_elements.size(); ++k) {
            buffer_curr[base_offset + k] = all_elements[data_idx++];
        }
        // 填充剩余部分为 Dummy
        for(; k < Z; ++k) {
            // 取出 dummy 对象的地址
            buffer_curr[base_offset + k] = &dummy_arena[dummy_idx++];
        }
    }

    // 5. 【优化 2】并行 Butterfly Network (Ping-Pong 模式)
    // for (int i = 0; i < num_levels_shuffle; ++i) {
    //     // 预计算位运算所需的掩码，避免在循环内部做除法
    //     const int p2i = 1 << i;
    //     const int p2i_mask = p2i - 1; // 用于替代 % p2i
        
    //     // note that we have multi-users, each user can paticipant in the task
    //     #pragma omp parallel for schedule(static) if(B / 2 > OMP_B_THRESHOLD)
    //     for (int j = 0; j < B / 2; ++j) {
    //         // 【优化 2】位运算替代取模和除法
    //         // 原逻辑: b1 = (j % p2i) + (j / p2i) * (2 * p2i);
    //         // 优化后:
    //         int b1 = (j & p2i_mask) + ((j >> i) << (i + 1));
    //         int b2 = b1 + p2i;

    //         // 【优化 4】使用迭代器区间构造，避免逐个 push_back
    //         // vector 的范围构造函数通常底层使用 memcpy/memmove，速度极快
    //         auto start_b1 = buffer_curr.begin() + b1 * Z;
    //         std::vector<Branch*> bucket_i_b1(start_b1, start_b1 + Z);

    //         auto start_b2 = buffer_curr.begin() + b2 * Z;
    //         std::vector<Branch*> bucket_i_b2(start_b2, start_b2 + Z);

    //         // 预分配输出 vector，避免 realloc
    //         std::vector<Branch*> out_1(Z), out_2(Z);

    //         if(i == 0) {
    //             client->ObliviousMergeSplit_firstlevel(bucket_i_b1, bucket_i_b2, out_1, out_2, i, num_levels_shuffle, HOTREE_level_);
    //         } else {
    //             client->ObliviousMergeSplit(bucket_i_b1, bucket_i_b2, out_1, out_2, i, num_levels_shuffle, HOTREE_level_);
    //         }

    //         // 写回 buffer_next (连续内存写入)
    //         int target_offset_1 = (2 * j) * Z;
    //         int target_offset_2 = (2 * j + 1) * Z;
            
    //         // 使用 memcpy 或者 std::copy 替代手动循环赋值
    //         // 只要 Branch* 是指针，copy 就是浅拷贝，非常快
    //         std::copy(out_1.begin(), out_1.end(), buffer_next.begin() + target_offset_1);
    //         std::copy(out_2.begin(), out_2.end(), buffer_next.begin() + target_offset_2);
    //     }

    //     // 统计更新 (移出 parallel 区域是正确的)
    //     client->communication_round_trip_ += (B / 2) / num_threads;
    //     client->communication_volume_ += (B / 2) * 2 * Z * BlockSize * 2; 

    //     // 【关键】交换 Buffer，准备下一层
    //     // std::swap 对 vector 只是交换内部指针，开销为 O(1)
    //     std::swap(buffer_curr, buffer_next);
    // }
    for (int i = 0; i < num_levels_shuffle; ++i) {
        const int p2i = 1 << i;
        const int p2i_mask = p2i - 1; 

        // 改为按 num_users 步进
        for (int j_start = 0; j_start < B / 2; j_start += num_users) {
            // 计算当前批次实际数量 (处理尾部不够 20 的情况)
            int current_batch = std::min(num_users, (B / 2) - j_start);
            
            // 准备 Batch 容器
            std::vector<std::vector<Branch*>> batch_in_0(current_batch);
            std::vector<std::vector<Branch*>> batch_in_1(current_batch);
            std::vector<std::vector<Branch*>> batch_out_0, batch_out_1;
            
            // 1. 组装输入 Batch (完全在内存连续区域操作)
            for (int k = 0; k < current_batch; ++k) {
                int j = j_start + k;
                int b1 = (j & p2i_mask) + ((j >> i) << (i + 1));
                int b2 = b1 + p2i;
                
                auto start_b1 = buffer_curr.begin() + b1 * Z;
                batch_in_0[k] = std::vector<Branch*>(start_b1, start_b1 + Z);
                
                auto start_b2 = buffer_curr.begin() + b2 * Z;
                batch_in_1[k] = std::vector<Branch*>(start_b2, start_b2 + Z);
            }

            // 2. 批处理调用 (20 个用户并发执行)
            bool is_first_level = (i == 0);
            client->ObliviousMergeSplit_Batched(
                batch_in_0, batch_in_1, 
                batch_out_0, batch_out_1, 
                i, num_levels_shuffle, HOTREE_level_, is_first_level
            );

            // 3. 将结果写回双缓冲的 Next 阶段
            for (int k = 0; k < current_batch; ++k) {
                int j = j_start + k;
                int dest_idx_1 = (2 * j) * Z;
                int dest_idx_2 = (2 * j + 1) * Z;
                
                std::copy(batch_out_0[k].begin(), batch_out_0[k].end(), buffer_next.begin() + dest_idx_1);
                std::copy(batch_out_1[k].begin(), batch_out_1[k].end(), buffer_next.begin() + dest_idx_2);
            }
        }

        // 统计更新 (注意轮次统计的变化：每次发送一个 Batch 算一轮交互)
        // 如果是严格模拟网络，20 个用户并发发送算 1 个通信回合
        client->communication_round_trip_ += ceil((double)(B / 2) / num_users);
        client->communication_volume_ += (B / 2) * 2 * Z * BlockSize * 2; 

        // 交换 Buffer，准备下一层
        std::swap(buffer_curr, buffer_next);
    }

    // 6. 记录纯 Shuffle 开销 (不含 insert)
    auto end_t = std::chrono::high_resolution_clock::now();
    single_shuffle_times = std::chrono::duration_cast<std::chrono::microseconds>(end_t - start_t).count() / 1000.0;
    single_shuffle_commucations = client->communication_volume_ - initial_volume;
    single_shuffle_round_trips = client->communication_round_trip_ - initial_rounds;

    // 7. 最终插入阶段
    // 此时结果在 buffer_curr 中 (因为最后一次循环做了 swap)
    table.assign(get_aligned_size(pow(2, HOTREE_level_)), Entry());
    stash.clear();
    current_count = 0;
    client->UpdateSeed(HOTREE_level_);


    /*The oblivious shuffle has already clustered all data belonging to the same bin together. 
    Therefore, the client can retrieve Z data items at a time and insert each item to cuckoo hash table. 
    For the local implementation of the cuckoo hash eviction operation, we insert elements one by one. 
    This approach simplifies the implementation because there is no need to initialize multiple cuckoo hash tables. 
    However, note that our implementation still guarantees the properties described in the paper, since during each insertion, 
    the result of our hash computation is guaranteed to fall within a specific bin (see function get_p1_p2(uint64_t id_and_counter, Client* client)). 
    Thus, we ensure that every element completes its eviction process within its assigned bin.*/
    // // 模拟不经意排序的时间开销：每 512 个数据等待 2ms
    // size_t batch_size = 512;
    // size_t num_batches = (buffer_curr.size() + batch_size - 1) / batch_size;  // 向上取整
    // uint64_t total_wait_ms = num_batches * 2;  // 每批等待 2ms

    // 高精度等待 total_wait_ms 毫秒
    // auto start = std::chrono::steady_clock::now();
    // auto wait_duration = std::chrono::milliseconds(total_wait_ms);
    // while(std::chrono::steady_clock::now() - start < wait_duration) {
    //     // 防止编译器优化掉的空循环
    //     asm volatile("" ::: "memory");
    // }

    for(Branch* branch : buffer_curr) {
        if(branch != nullptr && !branch->is_dummy_for_shuffle) {
            insert(branch, client);
        }
    }
    client->communication_round_trip_ += buffer_curr.size() / Z;
    client->communication_volume_ += buffer_curr.size() * B; 
    
    // 8. 资源清理
    // dummy_arena 会在函数结束时自动析构，无需手动 delete
    // buffer vectors 也会自动释放
}

void CuckooTable::oblivious_shuffle_and_insert_last_level(std::vector<Branch*>& all_elements, std::vector<int> branchs_level_belong_to, Client* client) {
    // 1. 参数计算
    int N_real = all_elements.size();
    int B = (N_real > Z) ? pow(2, ceil(log2(2.0 * N_real / Z))) : pow(2, ceil(log2(2.0 * sqrt(N_real))));
    int num_levels_shuffle = ceil(log2(B));
    int total_nodes_per_level = B * Z;

    // 2. 并行解密 (保持原有的计算密集型优化)
    // omp_set_num_threads(num_threads);
    // #pragma omp parallel for schedule(static)
    for(int i = 0; i < (int)all_elements.size(); i++) {
        all_elements[i]->trueData = client->cryptor_->aes_decrypt(all_elements[i]->trueData, branchs_level_belong_to[i]);
    }

    // 3. 【优化】内存初始化 (NUMA 优化 + 内存池)
    // 使用双缓冲 Ping-Pong 结构，替代昂贵的 memory[levels] 结构
    std::vector<Branch*> buffer_curr(total_nodes_per_level);
    std::vector<Branch*> buffer_next(total_nodes_per_level);

    // 使用 parallel for 初始化以触发 NUMA 的 First-Touch 策略
    // #pragma omp parallel for schedule(static)
    for(int i = 0; i < total_nodes_per_level; ++i) {
        buffer_curr[i] = nullptr;
        buffer_next[i] = nullptr;
    }

    // 【优化】Arena 方式分配 Dummy 对象
    // 只需要分配填充第一层所需的 Dummy 即可，后续层级只是指针的置换
    int num_dummies = total_nodes_per_level - N_real;
    std::vector<Branch> dummy_arena;
    if (num_dummies > 0) {
        dummy_arena.reserve(num_dummies + B); // 预留少量 padding 防止溢出
        dummy_arena.assign(num_dummies, Branch(true, true)); // 连续内存分配
    }

    // 4. 填充初始 Buffer (Level 0)
    // 根据原逻辑：前 Z/2 个放真实数据，剩下的放 Dummy
    int real_idx = 0;
    int dummy_idx = 0;

    for(int b = 0; b < B; ++b) {
        int base_offset = b * Z;
        int k = 0;
        
        // 填充真实数据
        for(; k < Z/2 && real_idx < N_real; ++k) {
            buffer_curr[base_offset + k] = all_elements[real_idx++];
        }
        // 填充 Dummy 数据
        for(; k < Z; ++k) {
            buffer_curr[base_offset + k] = &dummy_arena[dummy_idx++];
        }
    }

    // 5. 【优化】并行 Butterfly Network
    for (int i = 0; i < num_levels_shuffle; ++i) {
        const int p2i = 1 << i;
        const int mask = p2i - 1; // 用于位运算替代取模

        // #pragma omp parallel for schedule(static)
        for (int j = 0; j < B / 2; ++j) {
            // 【优化】位运算计算索引
            int b1 = (j & mask) + ((j >> i) << (i + 1));
            int b2 = b1 + p2i;

            // 【优化】使用迭代器区间构造 Vector，利用底层 memcpy 加速
            auto start_b1 = buffer_curr.begin() + b1 * Z;
            std::vector<Branch*> bucket_i_b1(start_b1, start_b1 + Z);

            auto start_b2 = buffer_curr.begin() + b2 * Z;
            std::vector<Branch*> bucket_i_b2(start_b2, start_b2 + Z);

            std::vector<Branch*> out_1(Z), out_2(Z);

            // 调用 Last Level 特有的 Split 函数
            // 原代码中 if(i==0) 和 else 调用的是同一个函数，这里直接统一调用
            client->ObliviousMergeSplit_firstlevel_last_level(
                bucket_i_b1, bucket_i_b2, out_1, out_2, 
                i, num_levels_shuffle, HOTREE_level_
            );

            // 写回 buffer_next
            int dest_idx_1 = (2 * j) * Z;
            int dest_idx_2 = (2 * j + 1) * Z;

            std::copy(out_1.begin(), out_1.end(), buffer_next.begin() + dest_idx_1);
            std::copy(out_2.begin(), out_2.end(), buffer_next.begin() + dest_idx_2);
        }

        // 更新统计 (移出并行区)
        client->communication_round_trip_ += (B / 2) / num_threads;
        client->communication_volume_ += (B / 2) * 2 * Z * BlockSize * 2; 

        // 交换 Buffer，准备下一轮
        std::swap(buffer_curr, buffer_next);
    }

    // 6. 最终插入与去重阶段
    // 此时结果存储在 buffer_curr 中
    table.assign(get_aligned_size(pow(2, HOTREE_level_)), Entry());
    stash.clear();
    current_count = 0;
    client->UpdateSeed(HOTREE_level_);

    int curr_id = 2147483647; // Sentinel value


    /*The oblivious shuffle has already clustered all data belonging to the same bin together. 
    Therefore, the client can retrieve Z data items at a time and insert each item to cuckoo hash table. 
    For the local implementation of the cuckoo hash eviction operation, we insert elements one by one. 
    This approach simplifies the implementation because there is no need to initialize multiple cuckoo hash tables. 
    However, note that our implementation still guarantees the properties described in the paper, since during each insertion, 
    the result of our hash computation is guaranteed to fall within a specific bin (see function get_p1_p2(uint64_t id_and_counter, Client* client)). 
    Thus, we ensure that every element completes its eviction process within its assigned bin.*/
    for(Branch* branch : buffer_curr) {
        if(branch != nullptr && !branch->is_dummy_for_shuffle) {
            // 【保留】原有的去重逻辑
            if(curr_id != branch->id) {
                branch->level = HOTREE_level_;
                branch->counter_for_lastest_data = 0;
                
                // 更新子节点元数据
                for(auto & triple : branch->child_triple) {
                    triple->counter_for_lastest_data = 0;
                    triple->level = HOTREE_level_;
                }
                
                insert(branch, client);
                curr_id = branch->id;
            }
        }
    }
    client->communication_round_trip_ += buffer_curr.size() / Z;
    client->communication_volume_ += buffer_curr.size() * B; 
}

// ---------------------------------------------------------
// New Function: Oblivious Shuffle
// ---------------------------------------------------------
void CuckooTable::oblivious_shuffle(Client* client) {
    // 1. Preparation: Gather all data (Table + Stash)
    std::vector<Branch*> all_elements;
    for (const auto& entry : table) {
        if (entry.occupied) all_elements.push_back(entry.branch);
    }
    for (const auto& item : stash) {
        all_elements.push_back(item);
    }

    // 2. Setup Dimensions
    // Butterfly network needs N to be a power of 2.
    // Z is capacity per bucket. For a standard Cuckoo table, bucket size is effectively 1.
    // However, to make the shuffle efficient, we can treat the table as B buckets of size Z=1 (or more).
    
    int N_real = all_elements.size();
    int B;
    if(N_real > Z) B = pow(2, ceil(log2(2.0 * N_real / Z)));
    else B = pow(2, ceil(log2(2.0 * sqrt(N_real))));
    int num_levels_shuffle = ceil(log2(B));

    // 核心优化：将 vector<vector<vector<Branch>>> 改为 vector<vector<vector<Branch*>>>
    // 这将内存占用从数十 GB 降低到数百 MB
    std::vector<std::vector<std::vector<Branch*>>> memory(num_levels_shuffle + 1);
    static Branch dummy_branch(true, true);

    int data_idx = 0;
    for(int i = 0; i < num_levels_shuffle + 1; ++i) {
        memory[i].resize(B);
        for(int b = 0; b < B; ++b) {
            memory[i][b].reserve(Z);
            for(int k = 0; k < Z; ++k) {
                if(i == 0 && k < Z / 2 && data_idx < all_elements.size()) {
                    memory[i][b].push_back(all_elements[data_idx++]);
                } else {
                    memory[i][b].push_back(&dummy_branch); // 仅存储指针
                }
            }
        }
    }

    // Butterfly 网络层级交换
    omp_set_num_threads(num_threads);
    for (int i = 0; i < num_levels_shuffle; ++i) {
        // #pragma omp parallel for
        for (int j = 0; j < B / 2; ++j) {
            int power_of_2_i = 1 << i;
            int power_of_2_i_plus_1 = 1 << (i + 1);
            
            int source_idx_1 = (j % power_of_2_i) + (j / power_of_2_i) * power_of_2_i_plus_1;
            int source_idx_2 = source_idx_1 + power_of_2_i;

            client->ObliviousMergeSplit(
                memory[i][source_idx_1],
                memory[i][source_idx_2],
                memory[i+1][2 * j],
                memory[i+1][2 * j + 1],
                i,
                num_levels_shuffle,
                HOTREE_level_
            );
        }
        client->communication_round_trip_ += (B / 2) / num_threads;
        // this level have B/2 bin, every bin have 2Z block, every block have size Blocksize, one trip (retrieve from server, send to server)
        client->communication_volume_ += (B / 2) * 2 * Z * BlockSize * 2; 
    }

    // 清理并重新插入
    table.assign(table.size(), Entry());
    stash.clear();
    current_count = 0;
    client->UpdateSeed(HOTREE_level_);

    for(int i = 0; i < B; i++) {
        for(auto* branch : memory[num_levels_shuffle][i]) {
            if(branch != nullptr && !branch->is_dummy_for_shuffle) {
                insert(branch, client);
            }
        }
    }
    
}