#include "client.h"
#include <random>

Client::Client(int L) 
    : current_total_stash_size_(0), gen(global_seed), dis(0, 0xFFFFFFFF) 
{
    
    cryptor_ = new Cryptor(L);
    counter_access_ = 0;
    counter_self_healing_access_ = 0;
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

    /*------------------------------------------for inter-query parallelism--------------------------------------------------*/
    num_users_ = num_users;
    user_stashes_.resize(num_users_);
    user_stash_mtxs_.reserve(num_users_); // reserve 避免 realloc
    for(int i = 0; i < num_users_; ++i) {
        user_stash_mtxs_.push_back(std::make_unique<std::mutex>());
    }

    // 初始化 Level Stash 锁
    vector_every_level_stash_.resize(L + 1);
    level_stash_mtxs_.reserve(L + 1);
    for(int i = 0; i <= L; ++i) {
        level_stash_mtxs_.push_back(std::make_unique<std::mutex>());
    }
    // 1. 初始化每个用户的随机数生成器
    user_gens_.reserve(num_users_);
    for(int i = 0; i < num_users_; ++i) {
        // 给每个线程不同的种子，防止生成相同的随机序列
        user_gens_.emplace_back(global_seed + i); 
    }
    
    /*------------------------------------------for inter-query parallelism--------------------------------------------------*/
}

// Client::Client(int L): gen(global_seed), dis(0, 0xFFFFFFFF) {
//     cryptor_ = new Cryptor(L);
//     counter_access_ = 0;
//     counter_self_healing_access_ = 0;
//     communication_volume_ = 0;
//     communication_round_trip_ = 0;
//     stash_.reserve(Z); 

//     // Ensure we have enough seeds. 
//     // Note: For Cuckoo Table shuffle, we might need a specific seed. 
//     // Here we assume level 0 seed is the primary Cuckoo seed.
//     for(int i = 0; i <= L; i++) {
//         vec_seed1_.push_back(dis(gen));
//         vec_seed2_.push_back(dis(gen));
//         if(i != L) {
//             vec_hotree_level_i_is_empty_.push_back(true);
//         }
//         else {
//             vec_hotree_level_i_is_empty_.push_back(false);
//         }
//     }
//     seed_shuffle_ = dis(gen);
//     vector_every_level_stash_.resize(L+1);
// }

void Client::UpdateSeed(size_t level_i) {
    seed_shuffle_ = dis(gen);
    vec_seed1_[level_i] = dis(gen);
    vec_seed2_[level_i] = dis(gen);
}

// src/client.cpp
Client::~Client() {
    delete cryptor_;
    cryptor_ = nullptr;

    for (auto& level_vec : vector_every_level_stash_) {
        level_vec.clear();
    }
    vector_every_level_stash_.clear();
    /*------------------------------------------for inter-query parallelism--------------------------------------------------*/
    for(auto& map : user_stashes_) {
        // 注意：Branch* 的内存管理需要明确，这里假设在 Eviction 或其他地方处理
        map.clear();
    }
    /*------------------------------------------for inter-query parallelism--------------------------------------------------*/
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
    // 1. 定义静态 Dummy，避免频繁 new/delete
    static Branch dummy_branch(true, true);

    std::vector<Branch*> pool;
    // 预留空间，避免 push_back 时的扩容开销
    pool.reserve(bucket_in_0.size() + bucket_in_1.size());

    // ============================================================
    // 第一阶段：并行解密 (必须严格保护共享的 dummy 节点)
    // ============================================================
    omp_set_num_threads(num_threads);

    // 处理 bucket_in_0
    #pragma omp parallel for
    for(size_t i = 0; i < bucket_in_0.size(); ++i) {
        Branch* s = bucket_in_0[i];
        // 核心修复：先判断是否为空或 Dummy，绝对不能对共享 Dummy 进行写操作！
        if (s != nullptr && !s->is_dummy_for_shuffle) {
            s->trueData = cryptor_->aes_decrypt(s->trueData, HOTREE_level);
        }
    }

    // 处理 bucket_in_1
    #pragma omp parallel for
    for(size_t i = 0; i < bucket_in_1.size(); ++i) {
        Branch* s = bucket_in_1[i];
        // 核心修复：同上
        if (s != nullptr && !s->is_dummy_for_shuffle) {
            s->trueData = cryptor_->aes_decrypt(s->trueData, HOTREE_level);
        }
    }

    // ============================================================
    // 第二阶段：串行收集 (指针拷贝极快，无需并行，避免锁竞争)
    // ============================================================
    for(auto* s : bucket_in_0) {
        if (s != nullptr && !s->is_dummy_for_shuffle) {
            pool.push_back(s);
        }
    }
    for(auto* s : bucket_in_1) {
        if (s != nullptr && !s->is_dummy_for_shuffle) {
            pool.push_back(s);
        }
    }
    
    // 准备输出容器
    std::vector<Branch*> real_elements_0;
    std::vector<Branch*> real_elements_1;
    real_elements_0.reserve(Z);
    real_elements_1.reserve(Z);

    // ============================================================
    // 第三阶段：并行加密 (同样跳过 dummy)
    // ============================================================
    // pool 中只包含非 dummy 元素，所以这里可以直接并行加密
    #pragma omp parallel for
    for(size_t i = 0; i < pool.size(); ++i) {
        Branch* elem = pool[i];
        elem->trueData = cryptor_->aes_encrypt(elem->trueData, HOTREE_level);
    }

    // ============================================================
    // 第四阶段：本地路由 (逻辑运算)
    // ============================================================
    int check_bit = num_levels_shuffle - 1 - level_index;
    int mask = 1 << check_bit;
    size_t mod_size = 1 << num_levels_shuffle; // 使用位运算替代 pow

    for (auto* elem : pool) {
        // compute_hash 是只读的，安全
        if (compute_hash(combine_unique(elem->id, elem->counter_for_lastest_data), mod_size) & mask) {
            real_elements_1.push_back(elem);
        } else {
            real_elements_0.push_back(elem);
        }
    }

    // ============================================================
    // 第五阶段：填充静态 Dummy 指针
    // ============================================================
    // 这里只填入指针，不创建新对象，非常安全且高效
    while (real_elements_0.size() < Z) real_elements_0.push_back(&dummy_branch);
    while (real_elements_1.size() < Z) real_elements_1.push_back(&dummy_branch);

    bucket_out_0 = std::move(real_elements_0);
    bucket_out_1 = std::move(real_elements_1);
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


void Client::ObliviousMergeSplit_last_level(
    std::vector<Branch*>& bucket_in_0,
    std::vector<Branch*>& bucket_in_1,
    std::vector<Branch*>& bucket_out_0,
    std::vector<Branch*>& bucket_out_1,
    int level_index,        // 新逻辑中不再使用，保留以兼容接口
    int num_levels_shuffle, // 新逻辑中不再使用，保留以兼容接口
    int HOTREE_level
) {
    // 1. 定义静态 Dummy，避免频繁 new/delete
    static Branch dummy_branch(true, true);

    std::vector<Branch*> pool;
    // 预留空间，避免 push_back 时的扩容开销
    pool.reserve(bucket_in_0.size() + bucket_in_1.size());

    // ============================================================
    // 第一阶段：并行解密 (必须严格保护共享的 dummy 节点)
    // ============================================================
    // 这一步非常关键，必须先解密才能进行后续的 ID 比较排序
    omp_set_num_threads(num_threads);

    // 处理 bucket_in_0
    #pragma omp parallel for
    for(size_t i = 0; i < bucket_in_0.size(); ++i) {
        Branch* s = bucket_in_0[i];
        // 核心修复：先判断是否为空或 Dummy，绝对不能对共享 Dummy 进行写操作！
        if (s != nullptr && !s->is_dummy_for_shuffle) {
            s->trueData = cryptor_->aes_decrypt(s->trueData, HOTREE_level);
        }
    }

    // 处理 bucket_in_1
    #pragma omp parallel for
    for(size_t i = 0; i < bucket_in_1.size(); ++i) {
        Branch* s = bucket_in_1[i];
        // 核心修复：同上
        if (s != nullptr && !s->is_dummy_for_shuffle) {
            s->trueData = cryptor_->aes_decrypt(s->trueData, HOTREE_level);
        }
    }

    // ============================================================
    // 第二阶段：串行收集 (只收集 Real 元素)
    // ============================================================
    for(auto* s : bucket_in_0) {
        if (s != nullptr && !s->is_dummy_for_shuffle) {
            pool.push_back(s);
        }
    }
    for(auto* s : bucket_in_1) {
        if (s != nullptr && !s->is_dummy_for_shuffle) {
            pool.push_back(s);
        }
    }

    // ============================================================
    // ★★★ 第三阶段：全值排序 (替代原本的位运算路由) ★★★
    // ============================================================
    // 此时 pool 中的数据已解密，可以安全读取 ID 和 Counter
    // 使用 std::sort 替代原本的 "if (hash & mask)"
    std::sort(pool.begin(), pool.end(), [](const Branch* a, const Branch* b) {
        // 安全性检查 (虽然 pool 里应该没有 nullptr)
        if (a == nullptr) return false;
        if (b == nullptr) return true;

        // 1. 第一优先级：按 ID 升序
        // 保证相同 ID 的元素紧挨在一起
        if (a->id != b->id) {
            return a->id < b->id;
        }

        // 2. 第二优先级：按 Counter 降序
        // 保证相同 ID 中，Counter 大的（最新的）排在前面
        return a->counter_for_lastest_data > b->counter_for_lastest_data;
    });

    // ============================================================
    // 第四阶段：并行加密 (同样跳过 dummy)
    // ============================================================
    // 排序后，Real 元素都在 pool 里，直接并行加密
    #pragma omp parallel for
    for(size_t i = 0; i < pool.size(); ++i) {
        Branch* elem = pool[i];
        elem->trueData = cryptor_->aes_encrypt(elem->trueData, HOTREE_level);
    }

    // ============================================================
    // 第五阶段：按顺序切分并填充 Dummy (Split)
    // ============================================================
    // 清空并预留输出空间
    bucket_out_0.clear();
    bucket_out_1.clear();
    bucket_out_0.reserve(Z);
    bucket_out_1.reserve(Z);

    size_t total_real = pool.size();

    // --- 填充 Bucket 0 (取排序后的前 Z 个) ---
    for (size_t i = 0; i < Z; ++i) {
        if (i < total_real) {
            bucket_out_0.push_back(pool[i]);
        } else {
            // Real 元素不够，补 Dummy
            bucket_out_0.push_back(&dummy_branch);
        }
    }

    // --- 填充 Bucket 1 (取排序后的后 Z 个) ---
    for (size_t i = 0; i < Z; ++i) {
        // 计算在 pool 中的索引：Z + i
        size_t pool_idx = Z + i; 
        
        if (pool_idx < total_real) {
            bucket_out_1.push_back(pool[pool_idx]);
        } else {
            // Real 元素耗尽，补 Dummy
            bucket_out_1.push_back(&dummy_branch);
        }
    }
}

void Client::ObliviousMergeSplit_firstlevel_last_level(
    std::vector<Branch*>& bucket_in_0,
    std::vector<Branch*>& bucket_in_1,
    std::vector<Branch*>& bucket_out_0,
    std::vector<Branch*>& bucket_out_1,
    int level_index,        // 在新逻辑中不再使用，但为了保持接口兼容保留
    int num_levels_shuffle, // 在新逻辑中不再使用，但为了保持接口兼容保留
    int HOTREE_level
) {
    // 1. 定义静态 Dummy，避免频繁 new/delete，且保证所有 Dummy 指向同一地址
    static Branch dummy_branch(true, true);

    std::vector<Branch*> pool;
    // 预留空间，避免 push_back 时的扩容开销
    pool.reserve(bucket_in_0.size() + bucket_in_1.size());

    // ============================================================
    // 第一阶段：设置线程 (解密逻辑需确保在调用此函数前或在此处完成)
    // ============================================================
    // 注意：std::sort 需要读取明文 ID。如果 bucket_in 中的数据是密文，
    // 请确保在放入 pool 之前或在此处调用 aes_decrypt。
    // 假设输入数据在此阶段 ID 是可读的。
    omp_set_num_threads(num_threads);

    // ============================================================
    // 第二阶段：串行收集 Real 元素 (过滤掉 Dummy)
    // ============================================================
    // 我们只把真实的元素放入池中进行排序
    for(auto* s : bucket_in_0) {
        if (s != nullptr && !s->is_dummy_for_shuffle) {
            pool.push_back(s);
        }
    }
    for(auto* s : bucket_in_1) {
        if (s != nullptr && !s->is_dummy_for_shuffle) {
            pool.push_back(s);
        }
    }

    // ============================================================
    // ★★★ 第三阶段：全值排序 (替代原本的路由逻辑) ★★★
    // ============================================================
    // 使用 std::sort 进行确定性排序：
    // 1. 相同的 ID 会聚在一起。
    // 2. 相同的 ID 中，Counter 大的（最新的）排在前面。
    std::sort(pool.begin(), pool.end(), [](const Branch* a, const Branch* b) {
        // 安全性检查 (虽然 pool 里应该没有 nullptr)
        if (a == nullptr) return false;
        if (b == nullptr) return true;

        // 1. 第一优先级：按 ID 升序
        if (a->id != b->id) {
            return a->id < b->id;
        }

        // 2. 第二优先级：按 Counter 降序 (最新的在前)
        return a->counter_for_lastest_data > b->counter_for_lastest_data;
    });

    // ============================================================
    // 第四阶段：并行加密 (只加密 Real 元素)
    // ============================================================
    // 排序后，pool 中的元素顺序已定，现在并行更新它们的密文
    #pragma omp parallel for
    for(size_t i = 0; i < pool.size(); ++i) {
        Branch* elem = pool[i];
        // 重新加密数据块
        elem->trueData = cryptor_->aes_encrypt(elem->trueData, HOTREE_level);
    }

    // ============================================================
    // 第五阶段：按顺序切分并填充 Dummy (Split)
    // ============================================================
    // 清空并预留输出空间
    bucket_out_0.clear();
    bucket_out_1.clear();
    bucket_out_0.reserve(Z);
    bucket_out_1.reserve(Z);

    size_t total_real = pool.size();

    // --- 填充 Bucket 0 (取排序后的前 Z 个) ---
    for (size_t i = 0; i < Z; ++i) {
        if (i < total_real) {
            bucket_out_0.push_back(pool[i]);
        } else {
            // Real 元素不够填满 Z，补 Dummy
            bucket_out_0.push_back(&dummy_branch);
        }
    }

    // --- 填充 Bucket 1 (取排序后的后 Z 个) ---
    for (size_t i = 0; i < Z; ++i) {
        // 计算在 pool 中的索引：Z + i
        size_t pool_idx = Z + i;
        
        if (pool_idx < total_real) {
            bucket_out_1.push_back(pool[pool_idx]);
        } else {
            // Real 元素耗尽，补 Dummy
            bucket_out_1.push_back(&dummy_branch);
        }
    }
    
    // 此时 bucket_out_0 和 bucket_out_1 已经包含了排序并加密好的数据
    // 且大小严格为 Z
}