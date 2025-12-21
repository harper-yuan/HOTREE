#include "client.h"
#include <random>

Client::Client(int L): gen(rd()), dis(0, 0xFFFFFFFF) {
    cryptor_ = new Cryptor(L);
    communication_volume_ = 0;
    communication_round_trip_ = 0;

    // Ensure we have enough seeds. 
    // Note: For Cuckoo Table shuffle, we might need a specific seed. 
    // Here we assume level 0 seed is the primary Cuckoo seed.
    for(int i = 0; i <= L; i++) {
        vec_seed1_.push_back(dis(gen));
        vec_seed2_.push_back(dis(gen));
        if(i != L) {
            vec_hotree_level_i_is_empty_.push_back(true);
        }
        else {
            vec_hotree_level_i_is_empty_.push_back(false);
        }
    }
    seed_shuffle_ = dis(gen);
    vector_every_level_stash_.resize(L+1);
}

void Client::UpdateSeed(size_t level_i) {
    seed_shuffle_ = dis(gen);
    vec_seed1_[level_i] = dis(gen);
    vec_seed2_[level_i] = dis(gen);
}

Client::~Client() {
    delete cryptor_;
    cryptor_ = nullptr;
}

size_t Client::compute_hash(uint64_t id, size_t mod_size) const {
    uint64_t k = id;
    const uint64_t m = 0xc6a4a7935bd1e995;
    const int r = 47;
    uint64_t h = seed_shuffle_ ^ (8 * m);
    k *= m; k ^= k >> r; k *= m;
    h ^= k; h *= m;
    h ^= h >> r; h *= m; h ^= h >> r;
    return h % mod_size;
}

size_t Client::compute_hash1(uint64_t id, size_t level_i, size_t mod_size) const {
    size_t seed = vec_seed1_[level_i];
    uint64_t k = id;
    const uint64_t m = 0xc6a4a7935bd1e995;
    const int r = 47;
    uint64_t h = seed ^ (8 * m);
    k *= m; k ^= k >> r; k *= m;
    h ^= k; h *= m;
    h ^= h >> r; h *= m; h ^= h >> r;
    return h % mod_size;
}

size_t Client::compute_hash2(uint64_t id, size_t level_i, size_t mod_size) const {
    size_t seed = vec_seed2_[level_i];
    uint64_t k = id;
    const uint64_t m = 0xc6a4a7935bd1e995;
    const int r = 47;
    uint64_t h = seed ^ (8 * m);
    k *= m; k ^= k >> r; k *= m;
    h ^= k; h *= m;
    h ^= h >> r; h *= m; h ^= h >> r;
    return h % mod_size;
}

void Client::ObliviousMergeSplit(
    std::vector<Branch>& bucket_in_0,
    std::vector<Branch>& bucket_in_1,
    std::vector<Branch>& bucket_out_0,
    std::vector<Branch>& bucket_out_1,
    int level_index,
    int num_levels_shuffle,
    int HOTREE_level
) {
    communication_round_trip_++;
    
    std::vector<Branch> pool;
    pool.reserve(2 * Z);
    // 1. Decrypt and download
    // Note: We use index 0 for encryption keys for simplicity, or pass level_index if keys rotate
    for(auto& s : bucket_in_0) {
        if (s.is_dummy_for_shuffle) continue; 
        s.trueData = cryptor_->aes_decrypt(s.trueData, HOTREE_level);
        s.level = HOTREE_level;
        pool.push_back(s);
    }
    for(auto& s : bucket_in_1) {
        if (s.is_dummy_for_shuffle) continue; 
        s.trueData = cryptor_->aes_decrypt(s.trueData, HOTREE_level);
        s.level = HOTREE_level;
        pool.push_back(s);
    }
    
    std::vector<Branch> real_elements_0;
    std::vector<Branch> real_elements_1;

    // 2. 本地逻辑分流
    int check_bit = num_levels_shuffle - 1 - level_index;
    int mask = 1 << check_bit;

    for (auto& elem : pool) {
        if (compute_hash(elem.id, pow(2,num_levels_shuffle)) & mask) { //this hash table have approximately 2^num_levels_shuffle bucket, assign every elem to its bucket
            elem.trueData = cryptor_->aes_encrypt(elem.trueData, HOTREE_level);
            real_elements_1.push_back(elem);
        } else {
            elem.trueData = cryptor_->aes_encrypt(elem.trueData, HOTREE_level);
            real_elements_0.push_back(elem);
        }
    }

    // 3. 填充 Dummy
    while (real_elements_0.size() < Z) real_elements_0.emplace_back(true, true); 
    while (real_elements_1.size() < Z) real_elements_1.emplace_back(true, true); 

    if (real_elements_0.size() > Z) throw std::overflow_error("The client capacity Z is too small");
    if (real_elements_1.size() > Z) throw std::overflow_error("The client capacity Z is too small");

    // 【修复】：将处理好的数据赋值回输出参数！
    bucket_out_0 = std::move(real_elements_0);
    bucket_out_1 = std::move(real_elements_1);
    // Update stats
    communication_volume_ += (bucket_in_0.size() + bucket_in_1.size()) * BlockSize; // Approximation
}