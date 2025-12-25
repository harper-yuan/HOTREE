#include "OHT.h"
#include <random>
#include <algorithm>
#include <iostream>
#include <cmath>

CuckooTable::CuckooTable(size_t initial_size, int HOTREE_level) : current_count(0) {
    HOTREE_level_ = HOTREE_level;
    if (initial_size < 4) initial_size = 4;
    size_t power_of_2 = 1;
    while(power_of_2 < initial_size) power_of_2 *= 2;
    table.resize(power_of_2);
    
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

Branch* CuckooTable::find(uint64_t id_and_counter, Client* client) {
    uint64_t id = id_and_counter>>32;
    // 1. Table Lookups
    size_t p1 = client->compute_hash1(id_and_counter, HOTREE_level_, table.size());
    if (table[p1].occupied && table[p1].branch != nullptr && table[p1].branch->id == id) {
        return table[p1].branch;
    }
    size_t p2 = client->compute_hash2(id_and_counter, HOTREE_level_, table.size());
    if (table[p2].occupied && table[p2].branch != nullptr && table[p2].branch->id == id) {
        return table[p2].branch;
    }

    // 2. Stash Lookup
    for (auto& item : stash) {
        if (item != nullptr && item->id == id) return item;
    }
    return nullptr;
}

Branch* CuckooTable::find_remove(uint64_t id, Client* client) {
    client->communication_round_trip_++;
    client->communication_volume_ += 2 * BlockSize;
    
    size_t p1 = client->compute_hash1(id, HOTREE_level_, table.size());
    if (table[p1].occupied && table[p1].branch != nullptr && table[p1].branch->id == id) {
        // 解密并重新加密模拟“逻辑移除”
        table[p1].branch->trueData = client->cryptor_->aes_decrypt(table[p1].branch->trueData, HOTREE_level_);
        table[p1].branch->trueData = client->cryptor_->aes_encrypt(table[p1].branch->trueData, HOTREE_level_);
        return table[p1].branch;
    }
    
    size_t p2 = client->compute_hash2(id, HOTREE_level_, table.size());
    if (table[p2].occupied && table[p2].branch != nullptr && table[p2].branch->id == id) {
        table[p2].branch->trueData = client->cryptor_->aes_decrypt(table[p2].branch->trueData, HOTREE_level_);
        table[p2].branch->trueData = client->cryptor_->aes_encrypt(table[p2].branch->trueData, HOTREE_level_);
        return table[p2].branch;
    }

    for (auto& item : stash) {
        if (item != nullptr && item->id == id) return item;
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
    if (find(combine_unique(item->id, item->counter_for_lastest_data), client) != nullptr) {
        return; 
    }

    static std::mt19937 rng(123); 
    for (int i = 0; i < MAX_KICKS; ++i) {
        size_t p1 = client->compute_hash1(combine_unique(item->id, item->counter_for_lastest_data), HOTREE_level_, table.size());
        if (!table[p1].occupied) {
            table[p1].branch = item;
            table[p1].occupied = true;
            current_count++;
            // if(1) {
            if(item->id == debug_id) {
                std::cout<<"In insert id "<< item->id <<" level "<< HOTREE_level_ <<" p1: "<<p1 << " seed: "<<client->vec_seed1_[HOTREE_level_]<<" table size "<< table.size()<< " counter "<< item->counter_for_lastest_data<<std::endl;
            }
            return;
        }

        size_t p2 = client->compute_hash2(combine_unique(item->id, item->counter_for_lastest_data), HOTREE_level_, table.size());
        if (!table[p2].occupied) {
            table[p2].branch = item;
            table[p2].occupied = true;
            current_count++;
            // if(1) {
            if(item->id == debug_id) {
                std::cout<<"In insert id "<< item->id <<" level "<<HOTREE_level_ <<" p2: "<<p2 << " seed: "<<client->vec_seed1_[HOTREE_level_]<<" table size"<< table.size()<< " counter "<< item->counter_for_lastest_data<<std::endl;
            }
            return;
        }

        // 随机踢出一个指针
        bool kick_p1 = (rng() % 2) == 0;
        size_t victim_pos = kick_p1 ? p1 : p2;
        std::swap(item, table[victim_pos].branch);
    }

    if (stash.size() < STASH_CAPACITY) {
        stash.push_back(item);
        if(item->id == debug_id) {
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

void CuckooTable::oblivious_shuffle_and_insert(std::vector<Branch*>& all_elements, std::vector<int> branchs_level_belong_to, Client* client) {

    table.assign(pow(2,HOTREE_level_), Entry());
    stash.clear();
    current_count = 0;
    client->UpdateSeed(HOTREE_level_);
    for(auto &elem : all_elements) {
        // if(elem->id == debug_id) {
        //     printf("id %d exist with counter %d in OHT.cpp\n", elem->id, elem->counter_for_lastest_data);
        // }
        insert(elem, client);
        // printf("insert id %d in level %d with table size counter %d\n", elem->id, HOTREE_level_, current_count);
    }
    return ;
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
    for(int i = 0; i < all_elements.size(); i++) {
        all_elements[i]->trueData = client->cryptor_->aes_decrypt(all_elements[i]->trueData, branchs_level_belong_to[i]);
    }

    std::vector<std::vector<std::vector<Branch*>>> memory(num_levels_shuffle + 1);

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
                }
            }
        }
    }

    // Butterfly network
    omp_set_num_threads(num_threads);
    for (int i = 0; i < num_levels_shuffle; ++i) {
        // #pragma omp parallel for
        for (int j = 0; j < B / 2; ++j) {
            int power_of_2_i = 1 << i;
            int power_of_2_i_plus_1 = 1 << (i + 1);
            
            int source_idx_1 = (j % power_of_2_i) + (j / power_of_2_i) * power_of_2_i_plus_1;
            int source_idx_2 = source_idx_1 + power_of_2_i;

            if(i == 0) {
                client->ObliviousMergeSplit_firstlevel(
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
        }
        client->communication_round_trip_ += (B / 2) / num_threads;
        // this level have B/2 bin, every bin have 2Z block, every block have size Blocksize, one trip (retrieve from server, send to server)
        client->communication_volume_ += (B / 2) * 2 * Z * BlockSize * 2; 
    }
    // Note that we have num_threads threads, Data between different threads is do not affect each other. We can send using one trip.
    
    // clear table and reinsert
    table.assign(pow(2,HOTREE_level_), Entry());
    stash.clear();
    current_count = 0;
    client->UpdateSeed(HOTREE_level_);

    
    /* According to the paper "Bucket Oblivious Sort:An Extremely Simple Oblivious Sort", 
    since the middle layer of the shuffle network is oblivious, the number of real elements (loads) 
    contained in each bin of the final layer of the shuffle network result can be leaked. 
    This information can still be transmitted from the client through the ObliviousMergeSplit function, 
    so there is no increase in communication volume and communication rounds (volume of loads is small compared to encrypted data and ignored). */
    
    // for(int i = 0; i < B; i++) {
    //     for(auto* branch : memory[num_levels_shuffle][i]) {
    //         if(branch != nullptr && !branch->is_dummy_for_shuffle) {
    //             insert(branch, client);
    //         }
    //     }
    // }
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