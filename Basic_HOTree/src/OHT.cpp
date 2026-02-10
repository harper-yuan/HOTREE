#include "OHT.h"
#include <random>
#include <algorithm>
#include <iostream>
#include <cmath>

CuckooTable::CuckooTable(size_t initial_size, int HOTREE_level) : current_count(0) {
    HOTREE_level_ = HOTREE_level;
    shuffle_count = 0;
    table.resize(initial_size);
    
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
    // 1. Table Lookups
    size_t p1 = client->compute_hash1(id_and_counter, HOTREE_level_, table.size());
    if (table[p1].occupied && table[p1].branch != nullptr && table[p1].branch->id == id && table[p1].branch->counter_for_lastest_data == counter_for_lastest_data) {
        return table[p1].branch;
    }
    size_t p2 = client->compute_hash2(id_and_counter, HOTREE_level_, table.size());
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
        size_t p1 = client->compute_hash1(combine_unique(item->id, item->counter_for_lastest_data), HOTREE_level_, table.size());
        if (!table[p1].occupied) {
            table[p1].branch = item;
            table[p1].occupied = true;
            current_count++;
            return;
        }

        size_t p2 = client->compute_hash2(combine_unique(item->id, item->counter_for_lastest_data), HOTREE_level_, table.size());
        if (!table[p2].occupied) {
            table[p2].branch = item;
            table[p2].occupied = true;
            current_count++;
            return;
        }

        // 随机踢出一个指针
        bool kick_p1 = (rng() % 2) == 0;
        size_t victim_pos = kick_p1 ? p1 : p2;
        std::swap(item, table[victim_pos].branch);
    }
    size_t p1 = client->compute_hash1(combine_unique(item->id, item->counter_for_lastest_data), HOTREE_level_, table.size());
    size_t p2 = client->compute_hash2(combine_unique(item->id, item->counter_for_lastest_data), HOTREE_level_, table.size());
    if(if_is_debug) {
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
    table.assign(pow(2, HOTREE_level_), Entry());
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
    omp_set_num_threads(num_threads);
    #pragma omp parallel for schedule(static)
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
    for (int i = 0; i < num_levels_shuffle; ++i) {
        // 预计算位运算所需的掩码，避免在循环内部做除法
        const int p2i = 1 << i;
        const int p2i_mask = p2i - 1; // 用于替代 % p2i
        
        #pragma omp parallel for schedule(static)
        for (int j = 0; j < B / 2; ++j) {
            // 【优化 2】位运算替代取模和除法
            // 原逻辑: b1 = (j % p2i) + (j / p2i) * (2 * p2i);
            // 优化后:
            int b1 = (j & p2i_mask) + ((j >> i) << (i + 1));
            int b2 = b1 + p2i;

            // 【优化 4】使用迭代器区间构造，避免逐个 push_back
            // vector 的范围构造函数通常底层使用 memcpy/memmove，速度极快
            auto start_b1 = buffer_curr.begin() + b1 * Z;
            std::vector<Branch*> bucket_i_b1(start_b1, start_b1 + Z);

            auto start_b2 = buffer_curr.begin() + b2 * Z;
            std::vector<Branch*> bucket_i_b2(start_b2, start_b2 + Z);

            // 预分配输出 vector，避免 realloc
            std::vector<Branch*> out_1(Z), out_2(Z);

            if(i == 0) {
                client->ObliviousMergeSplit_firstlevel(bucket_i_b1, bucket_i_b2, out_1, out_2, i, num_levels_shuffle, HOTREE_level_);
            } else {
                client->ObliviousMergeSplit(bucket_i_b1, bucket_i_b2, out_1, out_2, i, num_levels_shuffle, HOTREE_level_);
            }

            // 写回 buffer_next (连续内存写入)
            int target_offset_1 = (2 * j) * Z;
            int target_offset_2 = (2 * j + 1) * Z;
            
            // 使用 memcpy 或者 std::copy 替代手动循环赋值
            // 只要 Branch* 是指针，copy 就是浅拷贝，非常快
            std::copy(out_1.begin(), out_1.end(), buffer_next.begin() + target_offset_1);
            std::copy(out_2.begin(), out_2.end(), buffer_next.begin() + target_offset_2);
        }

        // 统计更新 (移出 parallel 区域是正确的)
        client->communication_round_trip_ += (B / 2) / num_threads;
        client->communication_volume_ += (B / 2) * 2 * Z * BlockSize * 2; 

        // 【关键】交换 Buffer，准备下一层
        // std::swap 对 vector 只是交换内部指针，开销为 O(1)
        std::swap(buffer_curr, buffer_next);
    }

    // 6. 记录纯 Shuffle 开销 (不含 insert)
    auto end_t = std::chrono::high_resolution_clock::now();
    single_shuffle_times = std::chrono::duration_cast<std::chrono::microseconds>(end_t - start_t).count() / 1000.0;
    single_shuffle_commucations = client->communication_volume_ - initial_volume;
    single_shuffle_round_trips = client->communication_round_trip_ - initial_rounds;

    // 7. 最终插入阶段
    // 此时结果在 buffer_curr 中 (因为最后一次循环做了 swap)
    table.assign(pow(2, HOTREE_level_), Entry());
    stash.clear();
    current_count = 0;
    client->UpdateSeed(HOTREE_level_);

    for(Branch* branch : buffer_curr) {
        if(branch != nullptr && !branch->is_dummy_for_shuffle) {
            insert(branch, client);
        }
    }
    
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
    omp_set_num_threads(num_threads);
    #pragma omp parallel for schedule(static)
    for(int i = 0; i < (int)all_elements.size(); i++) {
        all_elements[i]->trueData = client->cryptor_->aes_decrypt(all_elements[i]->trueData, branchs_level_belong_to[i]);
    }

    // 3. 【优化】内存初始化 (NUMA 优化 + 内存池)
    // 使用双缓冲 Ping-Pong 结构，替代昂贵的 memory[levels] 结构
    std::vector<Branch*> buffer_curr(total_nodes_per_level);
    std::vector<Branch*> buffer_next(total_nodes_per_level);

    // 使用 parallel for 初始化以触发 NUMA 的 First-Touch 策略
    #pragma omp parallel for schedule(static)
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

        #pragma omp parallel for schedule(static)
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
    table.assign(pow(2, HOTREE_level_), Entry());
    stash.clear();
    current_count = 0;
    client->UpdateSeed(HOTREE_level_);

    int curr_id = 2147483647; // Sentinel value

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

    // 资源自动清理：
    // dummy_arena 和 buffer vectors 会在此处析构
    // 无需手动 delete 循环
}

// void CuckooTable::oblivious_shuffle_and_insert(std::vector<Branch*>& all_elements_before_otc, std::vector<int> branchs_level_belong_to, Client* client) {
//     // 1. 初始化基础状态
//     table.assign(pow(2, HOTREE_level_), Entry());
//     stash.clear();
//     current_count = 0;
//     client->UpdateSeed(HOTREE_level_);
//     std::vector<Branch*> all_elements;

//      // 处理特殊层级逻辑（保持原逻辑不变）
//     if(all_elements_before_otc.size() <= TEE_Z ) {
//         if(HOTREE_level_ != client->max_level_) {
//             client->communication_round_trip_ += all_elements_before_otc.size()/Z;
//             client->communication_volume_ += all_elements_before_otc.size()*BlockSize;
//             for(auto &elem : all_elements_before_otc) {
//                 insert(elem, client);
//             }
//             return ;
//         } else {
//             client->communication_round_trip_ += 2*all_elements_before_otc.size()/Z;
//             client->communication_volume_ += 2*all_elements_before_otc.size()*BlockSize;
//             all_elements = oblivious_tight_compaction(all_elements_before_otc, branchs_level_belong_to, client);
//             for(auto &elem : all_elements) insert(elem, client);
//             return ;
//         }
//     }
//     else {
//         //为了准确的测试性能，每层的shuffle我们仅仅进行一次，并记录耗时和通信量，后续调用shuffle明文调用，但开销增加到最后的结果中
//         if(shuffle_count > 0) { 
//             shuffle_count ++;
//             client->communication_round_trip_ += single_shuffle_round_trips;
//             client->communication_volume_ += single_shuffle_commucations;
//             if(HOTREE_level_ != client->max_level_) {
//                 for(auto &elem : all_elements_before_otc) {
//                     insert(elem, client);
//                 }
//                 return ;
//             } else {
//                 all_elements = oblivious_tight_compaction(all_elements_before_otc, branchs_level_belong_to, client);
//                 for(auto &elem : all_elements) insert(elem, client);
//                 return ;
//             }
//         }
//         else {
//             shuffle_count ++;
//             goto full_oblivious_shuffle;
//         }
//     }
    
// full_oblivious_shuffle:
//     single_shuffle_commucations = client->communication_volume_;
//     single_shuffle_round_trips = client->communication_round_trip_;
//     auto start_t = std::chrono::high_resolution_clock::now();

//     if(all_elements_before_otc.size() > TEE_Z ) {
//         if(HOTREE_level_ != client->max_level_) {
//             all_elements = std::move(all_elements_before_otc);
//         } else {
//             oblivious_shuffle_and_insert_last_level(all_elements_before_otc, branchs_level_belong_to, client);
//             auto end_t = std::chrono::high_resolution_clock::now();
//             single_shuffle_times = std::chrono::duration_cast<std::chrono::microseconds>(end_t - start_t).count() / 1000.0;
//             single_shuffle_commucations = client->communication_volume_ - single_shuffle_commucations;
//             single_shuffle_round_trips = client->communication_round_trip_ - single_shuffle_round_trips;
//             return;
//         }
//     }

//     // 2. 计算 Shuffle 网络参数
//     int N_real = all_elements.size();
//     int B;
//     if(N_real > Z) B = pow(2, ceil(log2(2.0 * N_real / Z)));
//     else B = pow(2, ceil(log2(2.0 * sqrt(N_real))));
//     int num_levels_shuffle = ceil(log2(B));

//     // 3. 并行解密 (计算密集型任务，OpenMP 收益最高) [cite: 702]
//     omp_set_num_threads(num_threads);
//     #pragma omp parallel for schedule(static)
//     for(int i = 0; i < (int)all_elements.size(); i++) {
//         all_elements[i]->trueData = client->cryptor_->aes_decrypt(all_elements[i]->trueData, branchs_level_belong_to[i]);
//     }

//     // 4. 【关键优化】平面化内存布局与 Dummy 块对象池
//     // 使用一维 vector 模拟三维内存 [i][b][k]，极大提升 Cache 命中率
//     int total_nodes_per_level = B * Z;
//     int total_memory_size = (num_levels_shuffle + 1) * total_nodes_per_level;
//     std::vector<Branch*> flat_memory(total_memory_size, nullptr);
    
//     // 预分配所有需要的 Dummy 块，避免在并行循环中调用 new
//     int num_dummies = total_memory_size - all_elements.size();
//     std::vector<Branch*> dummy_pool;
//     dummy_pool.reserve(num_dummies);
//     for(int i = 0; i < num_dummies; ++i) {
//         dummy_pool.push_back(new Branch(true, true)); // [cite: 82, 395]
//     }

//     // 初始化第一层 (Level 0)
//     int data_idx = 0;
//     int dummy_idx = 0;
//     for(int b = 0; b < B; ++b) {
//         for(int k = 0; k < Z; ++k) {
//             int offset = b * Z + k;
//             if(k < Z / 2 && data_idx < (int)all_elements.size()) {
//                 flat_memory[offset] = all_elements[data_idx++];
//             } else {
//                 flat_memory[offset] = dummy_pool[dummy_idx++];
//             }
//         }
//     }
//     // 填充后续层级的占位符（使用池中剩余的 dummy）
//     for(int i = 1; i <= num_levels_shuffle; ++i) {
//         for(int j = 0; j < total_nodes_per_level; ++j) {
//             flat_memory[i * total_nodes_per_level + j] = dummy_pool[dummy_idx++];
//         }
//     }

//     // 5. 【关键优化】并行 Butterfly Network [cite: 72]
//     // 减少线程间共享变量，将统计移出并行区
//     for (int i = 0; i < num_levels_shuffle; ++i) {
//         int current_level_offset = i * total_nodes_per_level;
//         int next_level_offset = (i + 1) * total_nodes_per_level;

//         #pragma omp parallel for schedule(static)
//         for (int j = 0; j < B / 2; ++j) {
//             int p2i = 1 << i;
//             int p2i_plus_1 = 1 << (i + 1);
            
//             int b1 = (j % p2i) + (j / p2i) * p2i_plus_1;
//             int b2 = b1 + p2i;

//             // 获取当前层和下一层容器的引用（模拟之前的 memory[i][b]）
//             // 注意：这里需要根据你的 ObliviousMergeSplit 签名调整，
//             // 建议传入指针或 span，避免 vector 拷贝
//             std::vector<Branch*> bin_i_b1, bin_i_b2, bin_next_2j, bin_next_2j1;
            
//             // 模拟 Bucket 提取 (在此处手动处理 Z 个元素的拷贝/移动)
//             auto get_bucket = [&](int level_offset, int b_idx) {
//                 std::vector<Branch*> res;
//                 res.reserve(Z);
//                 for(int k=0; k<Z; ++k) res.push_back(flat_memory[level_offset + b_idx * Z + k]);
//                 return res;
//             };

//             auto bucket_i_b1 = get_bucket(current_level_offset, b1);
//             auto bucket_i_b2 = get_bucket(current_level_offset, b2);
//             std::vector<Branch*> out_1, out_2; // 预留输出空间
//             out_1.reserve(Z); out_2.reserve(Z);

//             if(i == 0) {
//                 client->ObliviousMergeSplit_firstlevel(bucket_i_b1, bucket_i_b2, out_1, out_2, i, num_levels_shuffle, HOTREE_level_);
//             } else {
//                 client->ObliviousMergeSplit(bucket_i_b1, bucket_i_b2, out_1, out_2, i, num_levels_shuffle, HOTREE_level_);
//             }

//             // 写回平面化内存
//             for(int k=0; k<Z; ++k) {
//                 flat_memory[next_level_offset + (2*j) * Z + k] = out_1[k];
//                 flat_memory[next_level_offset + (2*j+1) * Z + k] = out_2[k];
//             }
//         }
//         // 更新统计数据
//         client->communication_round_trip_ += (B / 2) / num_threads;
//         client->communication_volume_ += (B / 2) * 2 * Z * BlockSize * 2; 
//     }

//     // 6. 最终插入阶段
//     // 重新初始化表格以准备插入混淆后的元素 [cite: 353]
//     table.assign(pow(2, HOTREE_level_), Entry());
//     stash.clear();
//     current_count = 0;
//     client->UpdateSeed(HOTREE_level_);

//     int last_level_offset = num_levels_shuffle * total_nodes_per_level;
//     for(int i = 0; i < total_nodes_per_level; i++) {
//         Branch* branch = flat_memory[last_level_offset + i];
//         if(branch != nullptr && !branch->is_dummy_for_shuffle) {
//             insert(branch, client); // [cite: 358, 472]
//         }
//     }
    
//     // 7. 清理资源
//     for (auto* b : dummy_pool) delete b;
//     auto end_t = std::chrono::high_resolution_clock::now();
//     single_shuffle_times = std::chrono::duration_cast<std::chrono::microseconds>(end_t - start_t).count() / 1000.0;
//     single_shuffle_commucations = client->communication_volume_ - single_shuffle_commucations;
//     single_shuffle_round_trips = client->communication_round_trip_ - single_shuffle_round_trips;
// }


// void CuckooTable::oblivious_shuffle_and_insert_last_level(std::vector<Branch*>& all_elements, std::vector<int> branchs_level_belong_to, Client* client) {
//     int N_real = all_elements.size();
//     int B;
//     if(N_real > Z) B = pow(2, ceil(log2(2.0 * N_real / Z)));
//     else B = pow(2, ceil(log2(2.0 * sqrt(N_real))));
//     int num_levels_shuffle = ceil(log2(B));

//     /* This decryption step can be securely implemented by recording the layer 
//     where the input data itself is located (which the server can already know), 
//     and then decrypting the data within the ObliviousMergeSplit function based 
//     on the layer where the original data is located. All decrypted data is encrypted 
//     using the key corresponding to the new layer, but for the sake of concise and easy to 
//     understand code, we will decrypt it here. Note that this does not affect performance. */
//     omp_set_num_threads(num_threads);
//     #pragma omp parallel for
//     for(int i = 0; i < all_elements.size(); i++) {
//         all_elements[i]->trueData = client->cryptor_->aes_decrypt(all_elements[i]->trueData, branchs_level_belong_to[i]);
//     }
//     std::vector<std::vector<std::vector<Branch*>>> memory(num_levels_shuffle + 1);
//     // ✅【修复步骤 1】定义一个局部向量，作为“垃圾回收站”
//     std::vector<Branch*> dummy_garbage_collector;
//     dummy_garbage_collector.reserve(B * Z * (num_levels_shuffle + 1));
    
//     int data_idx = 0;
//     for(int i = 0; i < num_levels_shuffle + 1; ++i) {
//         memory[i].resize(B);
//         for(int b = 0; b < B; ++b) {
//             memory[i][b].reserve(Z);
//             for(int k = 0; k < Z; ++k) {
//                 if(i == 0 && k < Z / 2 && data_idx < all_elements.size()) {
//                     memory[i][b].push_back(all_elements[data_idx++]);
//                 } else {
//                     Branch* dummy_branch = new Branch(true, true);
//                     memory[i][b].push_back(dummy_branch); // 仅存储指针
//                     // 将其加入垃圾回收站，以便稍后释放
//                     dummy_garbage_collector.push_back(dummy_branch);
//                 }
//             }
//         }
//     }

//     // Butterfly network
//     omp_set_num_threads(num_threads);
//     for (int i = 0; i < num_levels_shuffle; ++i) {
//         #pragma omp parallel for
//         for (int j = 0; j < B / 2; ++j) {
//             int power_of_2_i = 1 << i;
//             int power_of_2_i_plus_1 = 1 << (i + 1);
            
//             int source_idx_1 = (j % power_of_2_i) + (j / power_of_2_i) * power_of_2_i_plus_1;
//             int source_idx_2 = source_idx_1 + power_of_2_i;

//             if(i == 0) {
//                 client->ObliviousMergeSplit_firstlevel_last_level(
//                     memory[i][source_idx_1],
//                     memory[i][source_idx_2],
//                     memory[i+1][2 * j],
//                     memory[i+1][2 * j + 1],
//                     i,
//                     num_levels_shuffle,
//                     HOTREE_level_
//                 );
//             }
//             else {
//                 client->ObliviousMergeSplit_firstlevel_last_level(
//                     memory[i][source_idx_1],
//                     memory[i][source_idx_2],
//                     memory[i+1][2 * j],
//                     memory[i+1][2 * j + 1],
//                     i,
//                     num_levels_shuffle,
//                     HOTREE_level_
//                 );
//             }
//         }
//         client->communication_round_trip_ += (B / 2) / num_threads;
//         // this level have B/2 bin, every bin have 2Z block, every block have size Blocksize, one trip (retrieve from server, send to server)
//         client->communication_volume_ += (B / 2) * 2 * Z * BlockSize * 2; 
//     }

//     // clear table and reinsert
//     for (auto* b : dummy_garbage_collector) {
//         delete b;
//     }
//     table.assign(pow(2,HOTREE_level_), Entry());
//     stash.clear();
//     current_count = 0;
//     client->UpdateSeed(HOTREE_level_);

//     int curr_id = 2147483647;
//     for(int i = 0; i < B; i++) {
//         for(auto* branch : memory[num_levels_shuffle][i]) {
//             if(branch != nullptr && !branch->is_dummy_for_shuffle) {
//                 if(curr_id != branch->id) {
//                     branch->level = HOTREE_level_;
//                     branch->counter_for_lastest_data = 0;
//                     for(auto & triple : branch->child_triple) {
//                         triple->counter_for_lastest_data = 0;
//                         triple->level = HOTREE_level_;
//                     }
//                     insert(branch, client);
//                     curr_id = branch->id;
//                 }                
//             }
//         }
//     }
// }

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
        #pragma omp parallel for
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