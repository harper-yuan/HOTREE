#include "OHT.h"
#include <random>
#include <algorithm>
#include <iostream>
#include <cmath>

CuckooTable::CuckooTable(size_t initial_size, int HOTREE_level) : current_count(0) {
    HOTREE_level_ = HOTREE_level;
    if (initial_size < 4) initial_size = 4;
    // Ensure size is power of 2 for easy bit-wise shuffling
    size_t power_of_2 = 1;
    while(power_of_2 < initial_size) power_of_2 *= 2;
    table.resize(power_of_2);
    
    stash.reserve(STASH_CAPACITY);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dis(0, 0xFFFFFFFF);
}

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
    result.push_back(&table[place1].branch);
    result.push_back(&table[place2].branch);
    return result;
}

Branch* CuckooTable::find(uint64_t id, Client* client) {
    // 1. Table Lookups
    size_t p1 = client->compute_hash1(id, HOTREE_level_, table.size());
    if (table[p1].occupied && table[p1].branch.id == id) {
        return &table[p1].branch;
    }
    size_t p2 = client->compute_hash2(id, HOTREE_level_, table.size());
    if (table[p2].occupied && table[p2].branch.id == id) {
        return &table[p2].branch;
    }

    // 2. Stash Lookup
    for (auto& item : stash) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

Branch* CuckooTable::find_remove(uint64_t id, Client* client) {
    // 1. Table Lookups
    client->communication_round_trip_++; // add one trip
    client->communication_volume_ += 2*BlockSize;
    size_t p1 = client->compute_hash1(id, HOTREE_level_, table.size());
    if (table[p1].occupied && table[p1].branch.id == id) {
        //if found replace the real block to dummy block
        table[p1].branch.trueData = client->cryptor_->aes_decrypt(table[p1].branch.trueData, HOTREE_level_);
        table[p1].branch.trueData = client->cryptor_->aes_encrypt(table[p1].branch.trueData, HOTREE_level_);
        return &table[p1].branch;
    }
    size_t p2 = client->compute_hash2(id, HOTREE_level_, table.size());
    if (table[p2].occupied && table[p2].branch.id == id) {
        table[p2].branch.trueData = client->cryptor_->aes_decrypt(table[p1].branch.trueData, HOTREE_level_);
        table[p2].branch.trueData = client->cryptor_->aes_encrypt(table[p1].branch.trueData, HOTREE_level_);
        return &table[p2].branch;
    }

    // 2. Stash Lookup
    for (auto& item : stash) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

void CuckooTable::insert(Branch& branch, Client* client) {
    if ((current_count + stash.size()) >= table.size() * 0.6) {
        rehash(table.size() * 2, client);
    }
    insert_internal(branch, client);
}

void CuckooTable::insert_internal(Branch item, Client* client) {
    if (Branch* existing = find(item.id, client)) {
        *existing = item;
        return;
    }

    static std::mt19937 rng(123); 
    for (int i = 0; i < MAX_KICKS; ++i) {
        size_t p1 = client->compute_hash1(item.id, HOTREE_level_, table.size());
        size_t p2 = client->compute_hash2(item.id, HOTREE_level_, table.size());

        if (!table[p1].occupied) {
            table[p1].branch = item;
            table[p1].occupied = true;
            current_count++;
            // if(item.id == -201) {
            //     std::cout<< "level"<< HOTREE_level_ <<"p1: "<<p1 <<" seed: "<<client->vec_seed1_[HOTREE_level_]<< " table size"<< table.size()<<std::endl;
            // }
            return;
        }

        
        if (!table[p2].occupied) {
            table[p2].branch = item;
            table[p2].occupied = true;
            current_count++;
            // if(item.id == -201) {
            //     std::cout<<"p2: "<< p2 <<" seed: "<<client->vec_seed1_[11]<<std::endl;
            // }
            return;
        }

        
        bool kick_p1 = (rng() % 2) == 0;
        size_t victim_pos = kick_p1 ? p1 : p2;
        std::swap(item, table[victim_pos].branch);
    }

    if (stash.size() < STASH_CAPACITY) {
        stash.push_back(item);
        return; 
    }
    rehash(table.size() * 2, client);
    insert_internal(item, client);
}

void CuckooTable::rehash(size_t new_size, Client* client) {
    std::vector<Entry> old_table = std::move(table);
    std::vector<Branch> old_stash = std::move(stash);

    table.resize(new_size);
    stash.clear();
    stash.reserve(STASH_CAPACITY);

    for(auto& e : table) e.occupied = false;
    current_count = 0;

    for (const auto& entry : old_table) {
        if (entry.occupied) insert_internal(entry.branch, client);
    }

    for (const auto& item : old_stash) {
        insert_internal(item, client);
    }
}

void CuckooTable::oblivious_shuffle_and_insert(std::vector<Branch> all_elements, Client* client) {
    // 2. Setup Dimensions
    // Butterfly network needs N to be a power of 2.
    // Z is capacity per bucket. For a standard Cuckoo table, bucket size is effectively 1.
    // However, to make the shuffle efficient, we can treat the table as B buckets of size Z=1 (or more).
    
    int N_real = all_elements.size();
    int B;
    if(N_real > Z) B = pow(2,ceil(log2(2*N_real/Z)));  // Number of buckets matches table size
    else B = pow(2,ceil(log2(2*(int)sqrt(N_real))));
    int num_levels_shuffle = ceil(log2(B));

    // 3. Encrypt Initial State into "Memory"
    // Memory[i] represents the i-th bucket in the oblivious shuffle
    std::vector<std::vector<std::vector<Branch>>> memory(num_levels_shuffle+1);
    
    // Distribute real elements sequentially initially (blind placement)
    // The shuffle will move them to their correct hash location
    int data_idx = 0;
    for(int i = 0; i < num_levels_shuffle+1; ++i) {
        memory[i].resize(B);
        for(int b=0; b < B; ++b) {
            for(int k=0; k < Z; ++k) {
                if(k < Z / 2 && i == 0 && data_idx < all_elements.size()) { //half is the true data
                    memory[i][b].push_back(all_elements[data_idx++]);
                }
                else {
                    memory[i][b].emplace_back(true, true);
                }
            }
        }
    }

    // 4. Butterfly Shuffle (Oblivious Random Bin Assignment)
    // Iterate levels of the butterfly network
    // At level 'i', we are sorting based on the i-th bit of the target hash
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
                num_levels_shuffle, // total_bits
                HOTREE_level_
            );
        }
    }

    stash.clear();
    current_count = 0;
    
    client->UpdateSeed(HOTREE_level_);
    for(int i = 0; i<B; i++) {
        for(auto& branch : memory[num_levels_shuffle][i]) { // all returned data are saved in the last level
            if(!branch.is_dummy_for_shuffle) {
                insert(branch, client);
            }
        }
    }
}

// ---------------------------------------------------------
// New Function: Oblivious Shuffle
// ---------------------------------------------------------
void CuckooTable::oblivious_shuffle(Client* client) {
    // 1. Preparation: Gather all data (Table + Stash)
    std::vector<Branch> all_elements;
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
    if(N_real > Z) B = pow(2,ceil(log2(2*N_real/Z)));  // Number of buckets matches table size
    else B = pow(2,ceil(log2(2*(int)sqrt(N_real))));
    int num_levels_shuffle = ceil(log2(B));

    // 3. Encrypt Initial State into "Memory"
    // Memory[i] represents the i-th bucket in the oblivious shuffle
    std::vector<std::vector<std::vector<Branch>>> memory(num_levels_shuffle+1);
    
    // Distribute real elements sequentially initially (blind placement)
    // The shuffle will move them to their correct hash location
    int data_idx = 0;
    for(int i = 0; i < num_levels_shuffle+1; ++i) {
        memory[i].resize(B);
        for(int b=0; b < B; ++b) {
            for(int k=0; k < Z; ++k) {
                if(k < Z / 2 && i == 0 && data_idx < all_elements.size()) { //half is the true data
                    memory[i][b].push_back(all_elements[data_idx++]);
                }
                else {
                    memory[i][b].emplace_back(true, true);
                }
            }
        }
    }

    // 4. Butterfly Shuffle (Oblivious Random Bin Assignment)
    // Iterate levels of the butterfly network
    // At level 'i', we are sorting based on the i-th bit of the target hash
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
                num_levels_shuffle, // total_bits
                HOTREE_level_
            );
        }
    }

    stash.clear();
    current_count = 0;
    
    client->UpdateSeed(HOTREE_level_);
    for(int i = 0; i<B; i++) {
        for(auto& branch : memory[num_levels_shuffle][i]) { // all returned data are saved in the last level
            if(!branch.is_dummy_for_shuffle) {
                insert(branch, client);
            }
        }
    }
}