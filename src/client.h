#pragma once
#include "cryptor.h"
#include "define.h"
#include "Branch.h"
#include <vector>
#include <unordered_map>

class Client {
public:
    Cryptor* cryptor_;
    std::unordered_map<int, Branch*> stash_;     // save the stash temprorary data
    std::vector<size_t> vec_seed1_; // save the 1st hash seed every level
    std::vector<size_t> vec_seed2_; // save the 2nd hash seed every level
    std::vector<bool> vec_hotree_level_i_is_empty_; // flag if the level i is empty
    std::vector<std::vector<Branch*>> vector_every_level_stash_; // save the stash data in every cuckoo table into local memory
    int min_level_; // save the label of the minimum level
    int max_level_; // save the label of the maximum level
    // hash seed for oblivious shuffle (assign the place according to the id). This seed need to change per shuffle, but we retain it for simplicity.
    size_t seed_shuffle_;
    double communication_round_trip_ = 0;
    double communication_volume_ = 0;
    int counter_access_ = 0;
    int counter_self_healing_access_ = 0;
    
    std::random_device rd;          // 随机设备
    std::mt19937 gen;               // 随机数生成引擎
    std::uniform_int_distribution<uint32_t> dis;  // 分布器
public:
    Client(int L);
    ~Client();

    // Helper: Replicate the MurmurHash logic to determine routing bits
    size_t compute_hash1(uint64_t id, size_t level_i, size_t mod_size) const;
    size_t compute_hash2(uint64_t id, size_t level_i, size_t mod_size) const;
    size_t compute_hash(uint64_t id, size_t mod_size) const;

    int get_first_empty_level();

    double CalcuTextRelevancy(std::vector<double> weight1, std::vector<double> weight2);

    double CalcuTestSPaceRele(Branch *n1, Branch *n2);
    // Core Logic: Decrypt -> Sort based on Hash Bit -> Encrypt
    void ObliviousMergeSplit(
        std::vector<Branch*>& bucket_in_0,
        std::vector<Branch*>& bucket_in_1,
        std::vector<Branch*>& bucket_out_0,
        std::vector<Branch*>& bucket_out_1,
        int level_index,
        int num_levels_shuffle,
        int HOTREE_level
    );
    void ObliviousMergeSplit_firstlevel(
        std::vector<Branch*>& bucket_in_0,
        std::vector<Branch*>& bucket_in_1,
        std::vector<Branch*>& bucket_out_0,
        std::vector<Branch*>& bucket_out_1,
        int level_index,
        int num_levels_shuffle,
        int HOTREE_level
    );
    void ObliviousMergeSplit_last_level(
        std::vector<Branch*>& bucket_in_0,
        std::vector<Branch*>& bucket_in_1,
        std::vector<Branch*>& bucket_out_0,
        std::vector<Branch*>& bucket_out_1,
        int level_index,
        int num_levels_shuffle,
        int HOTREE_level
    );
    void ObliviousMergeSplit_firstlevel_last_level(
        std::vector<Branch*>& bucket_in_0,
        std::vector<Branch*>& bucket_in_1,
        std::vector<Branch*>& bucket_out_0,
        std::vector<Branch*>& bucket_out_1,
        int level_index,
        int num_levels_shuffle,
        int HOTREE_level
    );
    void UpdateSeed(size_t level_i);
    // 生成随机数的函数
    size_t getRandomIndex(size_t mod_size) {
        // 创建分布，范围是 [0, size()-1]
        std::uniform_int_distribution<size_t> dist(0, mod_size-1); // range [0, mod_size-1]
        return dist(gen);
    }
};