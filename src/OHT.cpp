#include "OHT.h"
#include <random>
#include <algorithm>
#include <iostream>
#include <cmath>

CuckooTable::CuckooTable(size_t initial_size, int HOTREE_level) : current_count(0) {
    HOTREE_level_ = HOTREE_level;
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

    // 处理特殊层级逻辑（保持原逻辑不变）
    if(all_elements_before_otc.size() <= TEE_Z ) {
        if(HOTREE_level_ != client->max_level_) {
            for(auto &elem : all_elements_before_otc) {
                insert(elem, client);
            }
            return ;
        } else {
            all_elements = oblivious_tight_compaction(all_elements_before_otc, branchs_level_belong_to, client);
            for(auto &elem : all_elements) insert(elem, client);
            return ;
        }
    }
    
    if(all_elements_before_otc.size() > TEE_Z ) {
        if(HOTREE_level_ != client->max_level_) {
            all_elements = std::move(all_elements_before_otc);
        } else {
            oblivious_shuffle_and_insert_last_level(all_elements_before_otc, branchs_level_belong_to, client);
            return;
        }
    }

    // 2. 计算 Shuffle 网络参数
    int N_real = all_elements.size();
    int B;
    if(N_real > Z) B = pow(2, ceil(log2(2.0 * N_real / Z)));
    else B = pow(2, ceil(log2(2.0 * sqrt(N_real))));
    int num_levels_shuffle = ceil(log2(B));

    // 3. 并行解密 (计算密集型任务，OpenMP 收益最高) [cite: 702]
    omp_set_num_threads(num_threads);
    #pragma omp parallel for schedule(static)
    for(int i = 0; i < (int)all_elements.size(); i++) {
        all_elements[i]->trueData = client->cryptor_->aes_decrypt(all_elements[i]->trueData, branchs_level_belong_to[i]);
    }

    // 4. 【关键优化】平面化内存布局与 Dummy 块对象池
    // 使用一维 vector 模拟三维内存 [i][b][k]，极大提升 Cache 命中率
    int total_nodes_per_level = B * Z;
    int total_memory_size = (num_levels_shuffle + 1) * total_nodes_per_level;
    std::vector<Branch*> flat_memory(total_memory_size, nullptr);
    
    // 预分配所有需要的 Dummy 块，避免在并行循环中调用 new
    int num_dummies = total_memory_size - all_elements.size();
    std::vector<Branch*> dummy_pool;
    dummy_pool.reserve(num_dummies);
    for(int i = 0; i < num_dummies; ++i) {
        dummy_pool.push_back(new Branch(true, true)); // [cite: 82, 395]
    }

    // 初始化第一层 (Level 0)
    int data_idx = 0;
    int dummy_idx = 0;
    for(int b = 0; b < B; ++b) {
        for(int k = 0; k < Z; ++k) {
            int offset = b * Z + k;
            if(k < Z / 2 && data_idx < (int)all_elements.size()) {
                flat_memory[offset] = all_elements[data_idx++];
            } else {
                flat_memory[offset] = dummy_pool[dummy_idx++];
            }
        }
    }
    // 填充后续层级的占位符（使用池中剩余的 dummy）
    for(int i = 1; i <= num_levels_shuffle; ++i) {
        for(int j = 0; j < total_nodes_per_level; ++j) {
            flat_memory[i * total_nodes_per_level + j] = dummy_pool[dummy_idx++];
        }
    }

    // 5. 【关键优化】并行 Butterfly Network [cite: 72]
    // 减少线程间共享变量，将统计移出并行区
    for (int i = 0; i < num_levels_shuffle; ++i) {
        int current_level_offset = i * total_nodes_per_level;
        int next_level_offset = (i + 1) * total_nodes_per_level;

        #pragma omp parallel for schedule(static)
        for (int j = 0; j < B / 2; ++j) {
            int p2i = 1 << i;
            int p2i_plus_1 = 1 << (i + 1);
            
            int b1 = (j % p2i) + (j / p2i) * p2i_plus_1;
            int b2 = b1 + p2i;

            // 获取当前层和下一层容器的引用（模拟之前的 memory[i][b]）
            // 注意：这里需要根据你的 ObliviousMergeSplit 签名调整，
            // 建议传入指针或 span，避免 vector 拷贝
            std::vector<Branch*> bin_i_b1, bin_i_b2, bin_next_2j, bin_next_2j1;
            
            // 模拟 Bucket 提取 (在此处手动处理 Z 个元素的拷贝/移动)
            auto get_bucket = [&](int level_offset, int b_idx) {
                std::vector<Branch*> res;
                res.reserve(Z);
                for(int k=0; k<Z; ++k) res.push_back(flat_memory[level_offset + b_idx * Z + k]);
                return res;
            };

            auto bucket_i_b1 = get_bucket(current_level_offset, b1);
            auto bucket_i_b2 = get_bucket(current_level_offset, b2);
            std::vector<Branch*> out_1, out_2; // 预留输出空间
            out_1.reserve(Z); out_2.reserve(Z);

            if(i == 0) {
                client->ObliviousMergeSplit_firstlevel(bucket_i_b1, bucket_i_b2, out_1, out_2, i, num_levels_shuffle, HOTREE_level_);
            } else {
                client->ObliviousMergeSplit(bucket_i_b1, bucket_i_b2, out_1, out_2, i, num_levels_shuffle, HOTREE_level_);
            }

            // 写回平面化内存
            for(int k=0; k<Z; ++k) {
                flat_memory[next_level_offset + (2*j) * Z + k] = out_1[k];
                flat_memory[next_level_offset + (2*j+1) * Z + k] = out_2[k];
            }
        }
        // 更新统计数据
        client->communication_round_trip_ += (B / 2) / num_threads;
        client->communication_volume_ += (B / 2) * 2 * Z * BlockSize * 2; 
    }

    // 6. 最终插入阶段
    // 重新初始化表格以准备插入混淆后的元素 [cite: 353]
    table.assign(pow(2, HOTREE_level_), Entry());
    stash.clear();
    current_count = 0;
    client->UpdateSeed(HOTREE_level_);

    int last_level_offset = num_levels_shuffle * total_nodes_per_level;
    for(int i = 0; i < total_nodes_per_level; i++) {
        Branch* branch = flat_memory[last_level_offset + i];
        if(branch != nullptr && !branch->is_dummy_for_shuffle) {
            insert(branch, client); // [cite: 358, 472]
        }
    }

    // 7. 清理资源
    for (auto* b : dummy_pool) delete b;
}

// void CuckooTable::oblivious_shuffle_and_insert(std::vector<Branch*>& all_elements_before_otc, std::vector<int> branchs_level_belong_to, Client* client) {
//     table.assign(pow(2, HOTREE_level_), Entry());
//     stash.clear();
//     current_count = 0;
//     client->UpdateSeed(HOTREE_level_);
//     std::vector<Branch*> all_elements;

//     if(all_elements_before_otc.size() <= TEE_Z ) {
//         if(HOTREE_level_ != client->max_level_) {
//             table.assign(pow(2,HOTREE_level_), Entry());
//             stash.clear();
//             current_count = 0;
//             client->UpdateSeed(HOTREE_level_);
//             for(auto &elem : all_elements_before_otc) {
//                 if(elem->id == debug_id && if_is_debug) {
//                     printf("id %d exist with counter %d in OHT.cpp\n", elem->id, elem->counter_for_lastest_data);
//                 }
//                 insert(elem, client);
//                 // printf("insert id %d in level %d with table size counter %d\n", elem->id, HOTREE_level_, current_count);
//             }
//             return ; 
//         }
//         else {
//             /*--------------------------------oblivous tight compaction-----------------------------------*/
//             all_elements = oblivious_tight_compaction(all_elements_before_otc, branchs_level_belong_to, client);
//             for(auto &elem : all_elements) {
//                 insert(elem, client);
//             }
//             return ;
//         }
//     }
//     if(all_elements_before_otc.size() > TEE_Z ) {
//         if(HOTREE_level_ != client->max_level_) {
//             all_elements = std::move(all_elements_before_otc);
//         }
//         else {
//             /*--------------------------------oblivous tight compaction-----------------------------------*/
//             // all_elements = oblivious_tight_compaction(all_elements_before_otc, branchs_level_belong_to, client);
//             // for(auto &elem : all_elements) {
//             //     insert(elem, client);
//             // }
//             // return ;
//             oblivious_shuffle_and_insert_last_level(all_elements_before_otc, branchs_level_belong_to, client);
//             return;
//         }
//     }
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
//                 client->ObliviousMergeSplit_firstlevel(
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
//                 client->ObliviousMergeSplit(
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
//     // Note that we have num_threads threads, Data between different threads is do not affect each other. We can send using one trip.
    
//     // clear table and reinsert
//     for (auto* b : dummy_garbage_collector) {
//         delete b;
//     }
//     table.assign(pow(2,HOTREE_level_), Entry());
//     stash.clear();
//     current_count = 0;
//     client->UpdateSeed(HOTREE_level_);

    
//     /* According to the paper "Bucket Oblivious Sort:An Extremely Simple Oblivious Sort", 
//     since the middle layer of the shuffle network is oblivious, the number of real elements (loads) 
//     contained in each bin of the final layer of the shuffle network result can be leaked. 
//     This information can still be transmitted from the client through the ObliviousMergeSplit function, 
//     so there is no increase in communication volume and communication rounds (volume of loads is small compared to encrypted data and ignored). */

//     for(int i = 0; i < B; i++) {
//         for(auto* branch : memory[num_levels_shuffle][i]) {
//             if(branch != nullptr && !branch->is_dummy_for_shuffle) {
//                 insert(branch, client);
//             }
//         }
//     }
    
// }

void CuckooTable::oblivious_shuffle_and_insert_last_level(std::vector<Branch*>& all_elements, std::vector<int> branchs_level_belong_to, Client* client) {
    int N_real = all_elements.size();
    int B;
    if(N_real > Z) B = pow(2, ceil(log2(2.0 * N_real / Z)));
    else B = pow(2, ceil(log2(2.0 * sqrt(N_real))));
    int num_levels_shuffle = ceil(log2(B));

    /* This decryption step can be securely implemented by recording the layer 
    where the input data itself is located (which the server can already know), 
    and then decrypting the data within the ObliviousMergeSplit function based 
    on the layer where the original data is located. All decrypted data is encrypted 
    using the key corresponding to the new layer, but for the sake of concise and easy to 
    understand code, we will decrypt it here. Note that this does not affect performance. */
    omp_set_num_threads(num_threads);
    #pragma omp parallel for
    for(int i = 0; i < all_elements.size(); i++) {
        all_elements[i]->trueData = client->cryptor_->aes_decrypt(all_elements[i]->trueData, branchs_level_belong_to[i]);
    }
    std::vector<std::vector<std::vector<Branch*>>> memory(num_levels_shuffle + 1);
    // ✅【修复步骤 1】定义一个局部向量，作为“垃圾回收站”
    std::vector<Branch*> dummy_garbage_collector;
    dummy_garbage_collector.reserve(B * Z * (num_levels_shuffle + 1));
    
    int data_idx = 0;
    for(int i = 0; i < num_levels_shuffle + 1; ++i) {
        memory[i].resize(B);
        for(int b = 0; b < B; ++b) {
            memory[i][b].reserve(Z);
            for(int k = 0; k < Z; ++k) {
                if(i == 0 && k < Z / 2 && data_idx < all_elements.size()) {
                    memory[i][b].push_back(all_elements[data_idx++]);
                } else {
                    Branch* dummy_branch = new Branch(true, true);
                    memory[i][b].push_back(dummy_branch); // 仅存储指针
                    // 将其加入垃圾回收站，以便稍后释放
                    dummy_garbage_collector.push_back(dummy_branch);
                }
            }
        }
    }

    // Butterfly network
    omp_set_num_threads(num_threads);
    for (int i = 0; i < num_levels_shuffle; ++i) {
        #pragma omp parallel for
        for (int j = 0; j < B / 2; ++j) {
            int power_of_2_i = 1 << i;
            int power_of_2_i_plus_1 = 1 << (i + 1);
            
            int source_idx_1 = (j % power_of_2_i) + (j / power_of_2_i) * power_of_2_i_plus_1;
            int source_idx_2 = source_idx_1 + power_of_2_i;

            if(i == 0) {
                client->ObliviousMergeSplit_firstlevel_last_level(
                    memory[i][source_idx_1],
                    memory[i][source_idx_2],
                    memory[i+1][2 * j],
                    memory[i+1][2 * j + 1],
                    i,
                    num_levels_shuffle,
                    HOTREE_level_
                );
            }
            else {
                client->ObliviousMergeSplit_firstlevel_last_level(
                    memory[i][source_idx_1],
                    memory[i][source_idx_2],
                    memory[i+1][2 * j],
                    memory[i+1][2 * j + 1],
                    i,
                    num_levels_shuffle,
                    HOTREE_level_
                );
            }
        }
        client->communication_round_trip_ += (B / 2) / num_threads;
        // this level have B/2 bin, every bin have 2Z block, every block have size Blocksize, one trip (retrieve from server, send to server)
        client->communication_volume_ += (B / 2) * 2 * Z * BlockSize * 2; 
    }

    // clear table and reinsert
    for (auto* b : dummy_garbage_collector) {
        delete b;
    }
    table.assign(pow(2,HOTREE_level_), Entry());
    stash.clear();
    current_count = 0;
    client->UpdateSeed(HOTREE_level_);

    int curr_id = 2147483647;
    for(int i = 0; i < B; i++) {
        for(auto* branch : memory[num_levels_shuffle][i]) {
            if(branch != nullptr && !branch->is_dummy_for_shuffle) {
                if(curr_id != branch->id) {
                    branch->level = HOTREE_level_;
                    branch->counter_for_lastest_data = 0;
                    for(auto & triple : branch->child_triple) {
                        triple->counter_for_lastest_data = 0;
                        triple->level = HOTREE_level_;
                    }
                    insert(branch, client);
                    curr_id = branch->id;
                }                
            }
        }
    }
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


// void CuckooTable::oblivious_shuffle_and_insert(std::vector<Branch*>& all_elements_before_otc, std::vector<int> branchs_level_belong_to, Client* client) {
//     table.assign(pow(2, HOTREE_level_), Entry());
//     stash.clear();
//     current_count = 0;
//     client->UpdateSeed(HOTREE_level_);
//     std::vector<Branch*> all_elements;

//     if(all_elements_before_otc.size() <= TEE_Z ) {
//         if(HOTREE_level_ != client->max_level_) {
//             table.assign(pow(2,HOTREE_level_), Entry());
//             stash.clear();
//             current_count = 0;
//             client->UpdateSeed(HOTREE_level_);
//             for(auto &elem : all_elements_before_otc) {
//                 if(elem->id == debug_id && if_is_debug) {
//                     printf("id %d exist with counter %d in OHT.cpp\n", elem->id, elem->counter_for_lastest_data);
//                 }
//                 insert(elem, client);
//                 // printf("insert id %d in level %d with table size counter %d\n", elem->id, HOTREE_level_, current_count);
//             }
//             return ; 
//         }
//         else {
//             /*--------------------------------oblivous tight compaction-----------------------------------*/
//             all_elements = oblivious_tight_compaction(all_elements_before_otc, branchs_level_belong_to, client);
//             for(auto &elem : all_elements) {
//                 insert(elem, client);
//             }
//             return ;
//         }
//     }
//     if(all_elements_before_otc.size() > TEE_Z ) {
//         if(HOTREE_level_ == client->max_level_) {
//             /*--------------------------------oblivous tight compaction-----------------------------------*/
//             all_elements = oblivious_tight_compaction(all_elements_before_otc, branchs_level_belong_to, client);
//         }
//         else {
//             all_elements = std::move(all_elements_before_otc);
//         }
//     }
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
//                 client->ObliviousMergeSplit_firstlevel(
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
//                 client->ObliviousMergeSplit(
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
//     // Note that we have num_threads threads, Data between different threads is do not affect each other. We can send using one trip.
    
//     // clear table and reinsert
//     for (auto* b : dummy_garbage_collector) {
//         delete b;
//     }
//     table.assign(pow(2,HOTREE_level_), Entry());
//     stash.clear();
//     current_count = 0;
//     client->UpdateSeed(HOTREE_level_);

    
//     /* According to the paper "Bucket Oblivious Sort:An Extremely Simple Oblivious Sort", 
//     since the middle layer of the shuffle network is oblivious, the number of real elements (loads) 
//     contained in each bin of the final layer of the shuffle network result can be leaked. 
//     This information can still be transmitted from the client through the ObliviousMergeSplit function, 
//     so there is no increase in communication volume and communication rounds (volume of loads is small compared to encrypted data and ignored). */
    
//     for(int i = 0; i < B; i++) {
//         for(auto* branch : memory[num_levels_shuffle][i]) {
//             if(branch != nullptr && !branch->is_dummy_for_shuffle) {
//                 insert(branch, client);
//             }
//         }
//     }
// }