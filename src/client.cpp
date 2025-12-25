#include "client.h"
#include <random>

// Client::Client(int L): gen(rd()), dis(0, 0xFFFFFFFF) {
Client::Client(int L): gen(global_seed), dis(0, 0xFFFFFFFF) {
    cryptor_ = new Cryptor(L);
    communication_volume_ = 0;
    communication_round_trip_ = 0;
    stash_.reserve(Z); 

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

int Client::get_first_empty_level() {
    int result_level;
    for(int level_i = min_level_; level_i <= max_level_; level_i++) {
        if(vec_hotree_level_i_is_empty_[level_i]) {
            return level_i;
        }
        else {
            if(level_i == max_level_) { //last level
                return max_level_;
            }
        }
    }
}

double Client::CalcuTextRelevancy(std::vector<double> weight1, std::vector<double> weight2) {
    double rele = 0;
    double sum1 = 0, sum2 = 0;
    size_t n = std::min(weight1.size(), weight2.size());
    for(size_t i = 0; i < n; i++) {
        rele += weight1[i] * weight2[i];
        sum1 += weight1[i] * weight1[i];
        sum2 += weight2[i] * weight2[i];
    }
    if (sum1 == 0 || sum2 == 0) return 0.0;
    return rele / (sqrt(sum1) * sqrt(sum2));
}

double Client::CalcuTestSPaceRele(Branch *n1, Branch *n2) {
    double text = CalcuTextRelevancy(n1->weight, n2->weight);
    double dist = n1->m_rect.MinDist(n2->m_rect);
    double spaceScore = 1.0 / (1.0 + dist); 
    double rele = ALPHA * spaceScore + (1.0 - ALPHA) * text;
    return rele;
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

// src/client.cpp
void Client::ObliviousMergeSplit(
    std::vector<Branch*>& bucket_in_0,
    std::vector<Branch*>& bucket_in_1,
    std::vector<Branch*>& bucket_out_0,
    std::vector<Branch*>& bucket_out_1,
    int level_index,
    int num_levels_shuffle,
    int HOTREE_level
) {
    // communication_round_trip_++;
    
    // 使用静态局部变量作为 Dummy 指针，避免反复创建对象
    static Branch dummy_branch(true, true);

    std::vector<Branch*> pool;
    pool.reserve(bucket_in_0.size() + bucket_in_1.size());

    // 1. 解密并下载真实数据
    for(auto* s : bucket_in_0) {
        if (s == nullptr || s->is_dummy_for_shuffle) continue; 
        s->trueData = cryptor_->aes_decrypt(s->trueData, HOTREE_level);
        pool.push_back(s);
    }
    for(auto* s : bucket_in_1) {
        if (s == nullptr || s->is_dummy_for_shuffle) continue;
        s->trueData = cryptor_->aes_decrypt(s->trueData, HOTREE_level);
        pool.push_back(s);
    }
    
    std::vector<Branch*> real_elements_0;
    std::vector<Branch*> real_elements_1;

    // 2. 本地路由逻辑
    int check_bit = num_levels_shuffle - 1 - level_index;
    int mask = 1 << check_bit;

    for (auto* elem : pool) {
        if (compute_hash(combine_unique(elem->id, elem->counter_for_lastest_data), pow(2, num_levels_shuffle)) & mask) {
            elem->trueData = cryptor_->aes_encrypt(elem->trueData, HOTREE_level);
            real_elements_1.push_back(elem);
        } else {
            elem->trueData = cryptor_->aes_encrypt(elem->trueData, HOTREE_level);
            real_elements_0.push_back(elem);
        }
    }

    // 3. 填充指针而不是填充对象
    while (real_elements_0.size() < Z) real_elements_0.push_back(&dummy_branch);
    while (real_elements_1.size() < Z) real_elements_1.push_back(&dummy_branch);

    bucket_out_0 = std::move(real_elements_0);
    bucket_out_1 = std::move(real_elements_1);
    
    // communication_volume_ += (bucket_in_0.size() + bucket_in_1.size()) * BlockSize;
}

void Client::ObliviousMergeSplit_firstlevel(
    std::vector<Branch*>& bucket_in_0,
    std::vector<Branch*>& bucket_in_1,
    std::vector<Branch*>& bucket_out_0,
    std::vector<Branch*>& bucket_out_1,
    int level_index,
    int num_levels_shuffle,
    int HOTREE_level
) {
    // 使用静态局部变量作为 Dummy 指针，避免反复创建对象
    static Branch dummy_branch(true, true);

    std::vector<Branch*> pool;
    pool.reserve(bucket_in_0.size() + bucket_in_1.size());

    // 1. 解密并下载真实数据
    for(auto* s : bucket_in_0) {
        if (s == nullptr || s->is_dummy_for_shuffle) continue; 
        // s->trueData = cryptor_->aes_decrypt(s->trueData, HOTREE_level);
        pool.push_back(s);
    }
    for(auto* s : bucket_in_1) {
        if (s == nullptr || s->is_dummy_for_shuffle) continue;
        // s->trueData = cryptor_->aes_decrypt(s->trueData, HOTREE_level);
        pool.push_back(s);
    }
    
    std::vector<Branch*> real_elements_0;
    std::vector<Branch*> real_elements_1;

    // 2. 本地路由逻辑
    int check_bit = num_levels_shuffle - 1 - level_index;
    int mask = 1 << check_bit;

    for (auto* elem : pool) {
        if (compute_hash(combine_unique(elem->id, elem->counter_for_lastest_data), pow(2, num_levels_shuffle)) & mask) {
            elem->trueData = cryptor_->aes_encrypt(elem->trueData, HOTREE_level);
            real_elements_1.push_back(elem);
        } else {
            elem->trueData = cryptor_->aes_encrypt(elem->trueData, HOTREE_level);
            real_elements_0.push_back(elem);
        }
    }

    // 3. 填充指针而不是填充对象
    while (real_elements_0.size() < Z) real_elements_0.push_back(&dummy_branch);
    while (real_elements_1.size() < Z) real_elements_1.push_back(&dummy_branch);

    bucket_out_0 = std::move(real_elements_0);
    bucket_out_1 = std::move(real_elements_1);
}