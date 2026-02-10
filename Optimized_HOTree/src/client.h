#pragma once
#include "cryptor.h"
#include "define.h"
#include "Branch.h"
#include <vector>
#include <unordered_map>

class Client {
public:
    Cryptor* cryptor_;

    /*------------------------------------------for inter-query parallelism--------------------------------------------------*/
    // 【修改 1】使用原子变量，保证多线程统计准确且无需加锁
    std::atomic<double> communication_round_trip_;
    std::atomic<double> communication_volume_;
    std::atomic<int> counter_access_;
    std::atomic<int> counter_self_healing_access_;
    
    // 【修改 2】每个用户(线程)一个独立的随机数生成器，避免锁竞争
    std::vector<std::mt19937> user_gens_;
    std::uniform_int_distribution<uint32_t> dis;

    // --- 基于用户的 Stash 结构 ---
    // user_stashes_[i] 是第 i 个用户的专属 stash
    int num_users_; 
    std::vector<std::unordered_map<int, Branch*>> user_stashes_;
    
    // 尽管每个用户写自己的 stash，但因为有“ID检测机制”（别人要读），所以还是需要锁
    std::vector<std::unique_ptr<std::mutex>> user_stash_mtxs_;
    
    // 全局原子计数器：所有用户的 stash 元素总和
    std::atomic<int> current_total_stash_size_{0};
    std::vector<std::unique_ptr<std::mutex>> level_stash_mtxs_;

    /*------------------------------------------for inter-query parallelism--------------------------------------------------*/
    std::vector<size_t> vec_seed1_; // save the 1st hash seed every level
    std::vector<size_t> vec_seed2_; // save the 2nd hash seed every level
    std::vector<bool> vec_hotree_level_i_is_empty_; // flag if the level i is empty
    std::vector<std::vector<Branch*>> vector_every_level_stash_; // save the stash data in every cuckoo table into local memory
    int min_level_; // save the label of the minimum level
    int max_level_; // save the label of the maximum level
    // hash seed for oblivious shuffle (assign the place according to the id). This seed need to change per shuffle, but we retain it for simplicity.
    size_t seed_shuffle_;
    
    std::random_device rd;          // 随机设备
    std::mt19937 gen;               // 随机数生成引擎
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
    int getRandomIndex(int range, int user_id) {
        // 使用对应线程的 generator，完全无锁且安全
        return dis(user_gens_[user_id]) % range;
    }
};