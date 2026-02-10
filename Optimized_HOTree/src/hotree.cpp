#include "hotree.h"
using namespace std;

// void HOTree::print_stash() {
//     for(auto const& [key, val] : client_->stash_) {
//         std::cout << "Key: " << key << std::endl;
//     }
// }

#include <unordered_set>

void HOTree::PerformGarbageCollection(int target_level) {
    // 1. 只有在最后一层（最大层）才进行清理
    if (target_level != client_->max_level_) return;

    if (if_is_debug) {
        std::cout << "[GC] Start Garbage Collection. Before: " << all_branchs.size() << " objects." << std::endl;
    }

    // 2. [Mark 阶段] 收集所有“存活”的指针
    // 存活的指针只存在于：该层的 Hash Table 和该层的 Stash
    std::unordered_set<Branch*> active_pointers;
    
    // 收集 Table 中的指针
    for (const auto& entry : vec_hashtable_[target_level]->table) {
        if (entry.occupied && entry.branch != nullptr) {
            active_pointers.insert(entry.branch);
        }
    }
    
    // 收集 Stash 中的指针 (注意：Eviction后，Stash已经移交回 client_->vector_every_level_stash_)
    for (auto* ptr : client_->vector_every_level_stash_[target_level]) {
        if (ptr != nullptr) {
            active_pointers.insert(ptr);
        }
    }

    // 3. [Sweep 阶段] 遍历全局池，保留活的，删除死的
    std::vector<Branch*> survivors;
    survivors.reserve(active_pointers.size());

    for (Branch* ptr : all_branchs) {
        // 如果指针在存活集合里，保留
        if (active_pointers.find(ptr) != active_pointers.end()) {
            survivors.push_back(ptr);
        } 
        // 否则，它是 Compaction 丢弃的旧版本或重复数据，彻底删除！
        else {
            delete ptr; 
        }
    }

    // 4. 更新 all_branchs 为幸存者列表
    all_branchs = std::move(survivors);

    if (if_is_debug) {
        std::cout << "[GC] End Garbage Collection. After: " << all_branchs.size() << " objects." << std::endl;
    }
}

void HOTree::clear_additional_oblivious_shuffle_time() {
    for(int i = client_->min_level_; i <= client_->max_level_; i++) {
        vec_hashtable_[i]->shuffle_count = 0;
    }
}
double HOTree::compute_additional_oblivious_shuffle_time() {
    double result = 0;
    for(int i = client_->min_level_; i <= client_->max_level_; i++) {
        // 计算当前层级的开销
        double current_level_time = (double)vec_hashtable_[i]->shuffle_count * vec_hashtable_[i]->single_shuffle_times;
        result += current_level_time;
    }
    return result;
}

void HOTree::findid(int target_id) {

    // 1. 遍历 vec_hashtable_ 中的每一层
    int flag = 1;
    for (int level = 0; level < vec_hashtable_.size(); ++level) {
        auto& current_table_ptr = vec_hashtable_[level];
        auto& current_table_stash = client_->vector_every_level_stash_[level];
        if (!current_table_ptr) continue;

        // 2. 检查该层的主表 (table)
        for (const auto& entry : current_table_ptr->table) {
            if (entry.occupied && entry.branch != nullptr && entry.branch->id == target_id) {
                flag = 0;
                std::cout << "[Found] ID: " << target_id
                          << " in Level: " << level 
                          << ", Counter: " << entry.branch->counter_for_lastest_data << std::endl;
                for(auto & triple : entry.branch->child_triple) {
                    std::cout<<" child id "<< triple->id
                             <<" child level "<<triple->level
                             <<" child counter "<< triple->counter_for_lastest_data<<std::endl;
                }
            }
        }

        for (const auto branch_ptr : current_table_ptr->stash) {
            if (branch_ptr != nullptr && branch_ptr->id == target_id) {
                flag = 0;
                std::cout << "[Found in Stash] ID: " << target_id 
                          << " in Level: " << level << " stash"
                          << ", Counter: " << branch_ptr->counter_for_lastest_data << std::endl;
                for(auto & triple : branch_ptr->child_triple) {
                    std::cout<<" child id "<< triple->id
                             <<" child level "<<triple->level
                             <<" child counter "<< triple->counter_for_lastest_data<<std::endl;
                }
            }
        }
        // 3. 检查该层的溢出区 (stash)
        for (const auto branch_ptr : client_->vector_every_level_stash_[level]) {
            if (branch_ptr != nullptr && branch_ptr->id == target_id) {
                flag = 0;
                std::cout << "[Found in Stash] ID: " << target_id 
                          << " in Level: " << level << " stash"
                          << ", Counter: " << branch_ptr->counter_for_lastest_data << std::endl;
                for(auto & triple : branch_ptr->child_triple) {
                    std::cout<<" child id "<< triple->id
                             <<" child level "<<triple->level
                             <<" child counter "<< triple->counter_for_lastest_data<<std::endl;
                }
            }
        }
    }
    if(flag)
    std::cout << "[Not Found] ID: " << target_id << " does not exist in any level." << std::endl;
}

HOTree::HOTree(const vector<string>& dict){
    // 初始化全局变量
    vec_hashtable_.clear();
    dic_str = dict;
    dic_map.clear();
    id_to_record_vec.clear();
    for (size_t i = 0; i < dict.size(); ++i) {
        dic_map[dict[i]] = (int)i;
    }
}

HOTree::~HOTree() {
    
    // 修改后：让 Branch 的析构函数自己负责 Triple 的清理
    for (auto* b : all_branchs) {
        if (b) delete b; // 这会自动调用 Branch::~Branch() 清理内部的 Triple
    }
    all_branchs.clear();
    
    // root 里的 Triple 是独立的，需要单独清理
    for (auto* t : root) delete t;
    root.clear();
    
    delete client_;
}

Client* HOTree::getClient() {
    return client_;
}
/* 
When client memory is full, merge this memory and all upper level data(until the first empty level).
Merging is non-oblivious, because we use oblivous shuffle to confuse the all queried data after merging
*/
void HOTree::Eviction(Client* client) {
    // cout<<"Stash have "<<client->stash_.size()<<" elems"<<endl;
    vector<Branch*> all_shuffled_branchs;
    vector<int> branchs_level_belong_to;
    int target_level = client_->get_first_empty_level();
    if(if_is_debug) {
        cout<<"---------------------------------------start shuffle---------------------------------------\n";
    }

    /*-------------------------Move data of cuckoo hash tables to a vector-------------------------------*/
    for(int level_i = client_->min_level_; level_i < target_level; level_i++) {
        if(!client->vec_hotree_level_i_is_empty_[level_i]) { // level_i is not empty
            // move the data in cuckoo table to a vector
            for(auto & entry : vec_hashtable_[level_i]->table) {
                if(entry.occupied) {
                    // entry.branch->trueData = client->cryptor_->aes_decrypt(entry.branch->trueData, level_i);
                    entry.branch->level = target_level;
                    all_shuffled_branchs.push_back(entry.branch);
                    branchs_level_belong_to.push_back(level_i);
                }
            }
            vec_hashtable_[level_i]->table.clear();

            // move the data in cuckoo table stash to a vector
            for(auto & elem : client_->vector_every_level_stash_[level_i]) {
                elem->level = target_level;
                elem->trueData = client->cryptor_->aes_encrypt(elem->trueData, target_level);
                all_shuffled_branchs.push_back(elem);
                branchs_level_belong_to.push_back(level_i);
            }
            client_->vector_every_level_stash_[level_i].clear();

            // if data have removed, clear hash table
            client->vec_hotree_level_i_is_empty_[level_i] = true; // level_i will be empty
            
            if(if_is_debug) {
                cout<<"level "<<level_i<<" is no empty"<<endl;
            }
        }
    }
    // if it is the greatest level, merge it and upper levels 
    if(target_level == client_->max_level_) {
        for(auto & entry : vec_hashtable_[target_level]->table) {
            if(entry.occupied) {
                // entry.branch->trueData = client->cryptor_->aes_decrypt(entry.branch->trueData, level_i);
                entry.branch->level = target_level;
                all_shuffled_branchs.push_back(entry.branch);
                branchs_level_belong_to.push_back(target_level);
            }
        }
        vec_hashtable_[target_level]->table.clear();
        // if data have removed, clear hash table
        
        for(auto & elem : client_->vector_every_level_stash_[target_level]) {
            elem->level = target_level;
            all_shuffled_branchs.push_back(elem);
            branchs_level_belong_to.push_back(target_level);
        }
        client_->vector_every_level_stash_[target_level].clear();

        for(auto & triple : root) {
            triple->level = target_level;
            triple->counter_for_lastest_data = 0;
        }
    }
    client->vec_hotree_level_i_is_empty_[target_level] = false; // level_i will be full

    /*-------------------------Move data of client stash to a vector-------------------------------*/
    for(int i = 0; i < client->num_users_; ++i) {
        auto& stash = client->user_stashes_[i];
        for(auto& [id, branch] : stash) {
             // 加密并加入 shuffle 队列
             branch->trueData = client->cryptor_->aes_encrypt(branch->trueData, target_level);
             branch->level = target_level;
             all_shuffled_branchs.push_back(branch);
             branchs_level_belong_to.push_back(target_level);
        }
        stash.clear(); // 清空 map
    }

    /*-------------------------update prediction level of all data-------------------------------*/
    for(auto & branch : all_shuffled_branchs) {
        for(auto & triple : branch->child_triple) {
            if(triple->level < target_level) {
                triple->level = target_level;
            }
        }
    }
    
    /*-------------------------oblivious shuffle-------------------------------*/
    vec_hashtable_[target_level]->oblivious_shuffle_and_insert(all_shuffled_branchs, branchs_level_belong_to, client); // this fuction includes updating hash seed. 
    
    /*-------------------------update the client stash for cuckoo table stash of target_level-------------------------------*/
    for(int i = 0; i < vec_hashtable_[target_level]->stash.size(); i++) {
        // move the stash to client, stash is so small that client is easy to save
        Branch* temp_branch = vec_hashtable_[target_level]->stash[i];
        temp_branch->trueData = client->cryptor_->aes_decrypt(temp_branch->trueData, target_level);
        client_->vector_every_level_stash_[target_level].push_back(temp_branch);
    }
    vec_hashtable_[target_level]->stash.clear();
    
    if(target_level == client->max_level_) {
        //This function promptly frees variables to prevent memory explosion. However, it incurs a performance overhead
        PerformGarbageCollection(target_level);
    }
    if(if_is_debug) {
        for(int level_i = client_->min_level_; level_i <= client_->max_level_; level_i++) {
        if(!client_->vec_hotree_level_i_is_empty_[level_i]) {
                string temp = "After Oblivious Shuffle & Insert level: "+level_i;
                vec_hashtable_[level_i]->print_table_status(temp,client_->vector_every_level_stash_[level_i]);
            }
        }
        cout<< all_shuffled_branchs.size() <<" data is shuffled and moved to level: "<<target_level<<endl;
        cout<<"---------------------------------------end shuffle---------------------------------------\n";
    }
}

Branch* HOTree::Access(uint64_t id, int counter_for_lastest_data, int level_i, int user_id) {
    uint64_t id_counter_combine = combine_unique(id, counter_for_lastest_data);
    Branch* result_branch = nullptr;
    
    // 【原子操作】无需加锁，直接 += 即可
    // client_->communication_round_trip_ = client_->communication_round_trip_ + 0.5; 
    // client_->counter_access_++; 

    for(int i = client_->min_level_; i < client_->vec_hotree_level_i_is_empty_.size(); i++) {
        if(client_->vec_hotree_level_i_is_empty_[i]) {
            continue; 
        }
        else {
            if(i == level_i) {
                // compute_hash 通常是纯计算，天然线程安全
                size_t p1 = client_->compute_hash1(id_counter_combine, i, vec_hashtable_[i]->getTableCapacity());
                size_t p2 = client_->compute_hash2(id_counter_combine, i, vec_hashtable_[i]->getTableCapacity());

                // 【原子操作】
                // client_->communication_volume_ = client_->communication_volume_ + BlockSize*2;

                // find_hotree 只要是只读查询，就是线程安全的
                auto vec_temp_branch = vec_hashtable_[i]->find_hotree(id, p1, p2);
                
                for(auto& elem : vec_temp_branch) {
                    if(elem != nullptr) {
                        if(elem->id == id && elem->counter_for_lastest_data == counter_for_lastest_data) {
                            result_branch = new Branch(elem);
                            
                            // 【关键】加锁保护 all_branchs
                            // 这是一个很小的临界区，只会锁这一下，不会影响整体并行度
                            {
                                std::lock_guard<std::mutex> lock(all_branchs_mtx_);
                                all_branchs.push_back(result_branch);
                            }
                        }
                    }
                }
            }
            else {
                // 【修改】传入 user_id 使用线程独立的随机生成器
                size_t p1 = client_->getRandomIndex(vec_hashtable_[i]->getTableCapacity(), user_id);
                size_t p2 = client_->getRandomIndex(vec_hashtable_[i]->getTableCapacity(), user_id);
                
                // 【原子操作】
                // client_->communication_volume_ = client_->communication_volume_ + BlockSize*2;
                
                auto vec_temp_branch = vec_hashtable_[i]->find_hotree(id, p1, p2);
            }
        }
    }
    return result_branch;
}

Branch* HOTree::Self_healing_Access(int id, int counter_for_lastest_data, int prediction_level, int user_id) {
    Branch* result_branch = nullptr;
    // 【原子操作】
    // client_->communication_round_trip_ = client_->communication_round_trip_ + 0.5; 
    // client_->counter_self_healing_access_++;

    // 1. 查找 Client Level Stash (需要加锁)
    // 之前在 Retrieve 里查过一次，这里是 Self-healing 的逻辑
    for(int i = client_->min_level_; i < client_->vec_hotree_level_i_is_empty_.size(); i++) {
        if(!client_->vec_hotree_level_i_is_empty_[i]) {
            if(result_branch == nullptr) {
                // 【关键】加锁访问 Level Stash
                std::lock_guard<std::mutex> lg(*client_->level_stash_mtxs_[i]);
                auto& level_stash = client_->vector_every_level_stash_[i];
                auto it = std::find_if(level_stash.begin(), level_stash.end(), [&](Branch* b) {
                    return b != nullptr && b->id == id && b->counter_for_lastest_data == counter_for_lastest_data;
                });
                if (it != level_stash.end()) {
                    result_branch = *it; 
                    level_stash.erase(it);
                }
            }
        }
    }
    if(result_branch != nullptr) return result_branch;

    // 2. 查找 Server Tables
    for(int i = prediction_level; i < client_->vec_hotree_level_i_is_empty_.size(); i++) {
        if(!client_->vec_hotree_level_i_is_empty_[i]) {
            if(result_branch == nullptr) {
                // ... hash 计算 ...
                size_t p1 = client_->compute_hash1(combine_unique(id, counter_for_lastest_data), i, vec_hashtable_[i]->getTableCapacity());
                size_t p2 = client_->compute_hash2(combine_unique(id, counter_for_lastest_data), i, vec_hashtable_[i]->getTableCapacity());

                auto vec_temp_branch = vec_hashtable_[i]->find_hotree(id, p1, p2);
                
                // 【原子操作】
                int level_num_is_not_empty = count(client_->vec_hotree_level_i_is_empty_.begin() + client_->min_level_, client_->vec_hotree_level_i_is_empty_.begin() + client_->max_level_ + 1, false);
                // client_->communication_volume_ = client_->communication_volume_ + level_num_is_not_empty*BlockSize*2;

                for(auto& elem : vec_temp_branch) {
                    if(elem != nullptr) {
                        elem->trueData = client_->cryptor_->aes_decrypt(elem->trueData, i);
                        if(elem->id == id && elem->counter_for_lastest_data == counter_for_lastest_data) {
                            result_branch = new Branch(elem);
                            
                            // 【关键】加锁
                            {
                                std::lock_guard<std::mutex> lock(all_branchs_mtx_);
                                all_branchs.push_back(result_branch);
                            }
                            return result_branch;
                        }
                    }   
                }
            }
        }
    }
    return result_branch;
}

//access some level with exactly level_i and access other levels with dummy
// Branch* HOTree::Access(uint64_t id, int counter_for_lastest_data, int level_i) {
//     uint64_t id_counter_combine = combine_unique(id, counter_for_lastest_data);
//     Branch* result_branch = nullptr;
    
//     client_->communication_round_trip_ += 0.5; // only one trip
//     client_->counter_access_ += 1;
//     // access every level if the client stash have not found target id data
//     for(int i = client_->min_level_; i < client_->vec_hotree_level_i_is_empty_.size(); i++) {
//         if(client_->vec_hotree_level_i_is_empty_[i]) {
//             continue; // empty level pass
//         }
//         else {
//             if(i == level_i) {
//                 size_t p1 = client_->compute_hash1(id_counter_combine, i, vec_hashtable_[i]->getTableCapacity());
//                 size_t p2 = client_->compute_hash2(id_counter_combine, i, vec_hashtable_[i]->getTableCapacity());

//                 if(id == debug_id && if_is_debug) {
//                     std::cout<<"In search level"<< level_i <<" p1: "<<p1 << " seed: "<<client_->vec_seed1_[level_i]<<" table size"<< vec_hashtable_[i]->getTableCapacity()<<std::endl;
//                 }

//                 auto vec_temp_branch = vec_hashtable_[i]->find_hotree(id, p1, p2);
//                 client_->communication_volume_ += BlockSize*2; // two blocks
//                 for(auto& elem : vec_temp_branch) {
//                     if(elem != nullptr) {
//                         if(elem->id == id && elem->counter_for_lastest_data == counter_for_lastest_data) {
//                             result_branch = new Branch(elem);
//                             // ✅【修复】新增这一行：将新对象交给 HOTree 管理
//                             all_branchs.push_back(result_branch);
//                         }
//                     }
//                 }
//             }
//             else {
//                 size_t p1 = client_->getRandomIndex(vec_hashtable_[i]->getTableCapacity());
//                 size_t p2 = client_->getRandomIndex(vec_hashtable_[i]->getTableCapacity());
//                 client_->communication_volume_ += BlockSize*2; // two blocks
//                 auto vec_temp_branch = vec_hashtable_[i]->find_hotree(id, p1, p2);
//             }
//         }
//     }
//     return result_branch;
// }

// Branch* HOTree::Self_healing_Access(int id, int counter_for_lastest_data, int prediction_level) {
//     Branch* result_branch = nullptr;
//     client_->communication_round_trip_ += 0.5; // only half trip, no need to put back
//     client_->counter_self_healing_access_ += 1;

//     /*--------------------------------find the data using standard Hierarchical ORAM, compute index using real id untill found---------------------*/
//     for(int i = client_->min_level_; i < client_->vec_hotree_level_i_is_empty_.size(); i++) {
//         if(client_->vec_hotree_level_i_is_empty_[i]) {
//             continue; // empty level pass
//         }
//         else {
//             // lookup cuckoo stash to find the id if data is not in hash table. If found, delete from cuckoo stash and move to stash
//             if(result_branch == nullptr) {
//                 auto& level_stash = client_->vector_every_level_stash_[i];
//                 auto it = std::find_if(level_stash.begin(), level_stash.end(), [&](Branch* b) {
//                     return b != nullptr && b->id == id && b->counter_for_lastest_data == counter_for_lastest_data;
//                 });
//                 if (it != level_stash.end()) {
//                     result_branch = *it; 
//                     level_stash.erase(it);
//                 }
//             }
//         }
//     }
//     if(result_branch != nullptr) return result_branch;

//     /*--------------------------------find the data on server tables---------------------*/
//     for(int i = prediction_level; i < client_->vec_hotree_level_i_is_empty_.size(); i++) {
//         if(client_->vec_hotree_level_i_is_empty_[i]) {
//             continue; // empty level pass
//         }
//         else {
//             if(result_branch == nullptr) {
//                 size_t p1 = client_->compute_hash1(combine_unique(id, counter_for_lastest_data), i, vec_hashtable_[i]->getTableCapacity());
//                 size_t p2 = client_->compute_hash2(combine_unique(id, counter_for_lastest_data), i, vec_hashtable_[i]->getTableCapacity());

//                 if(id == debug_id && if_is_debug) {
//                     std::cout<<"In self heal search level"<< i <<" p1: "<<p1 << " seed: "<<client_->vec_seed1_[i]<<" table size"<< vec_hashtable_[i]->getTableCapacity()<<std::endl;
//                 }
//                 auto vec_temp_branch = vec_hashtable_[i]->find_hotree(id, p1, p2);
//                 int level_num_is_not_empty = count(client_->vec_hotree_level_i_is_empty_.begin() + client_->min_level_, client_->vec_hotree_level_i_is_empty_.begin() + client_->max_level_ + 1, false);
//                 client_->communication_volume_ += level_num_is_not_empty*BlockSize*2; // two blocks
//                 for(auto& elem : vec_temp_branch) {
//                     if(elem != nullptr) {
//                         elem->trueData = client_->cryptor_->aes_decrypt(elem->trueData, i);
//                         if(elem->id == id && elem->counter_for_lastest_data == counter_for_lastest_data) {
//                             result_branch = new Branch(elem);
//                             // ✅【修复】新增这一行：将新对象交给 HOTree 管理
//                             all_branchs.push_back(result_branch);
//                             return result_branch;
//                         }
//                     }   
//                 }
//             }
//             // else { //dummy lookup but no need to decrypt beacause data have found
//             //     size_t p1 = client_->getRandomIndex(vec_hashtable_[i]->getTableCapacity());
//             //     size_t p2 = client_->getRandomIndex(vec_hashtable_[i]->getTableCapacity());
//             //     client_->communication_volume_ += BlockSize*2; // two blocks
//             //     auto vec_temp_branch = vec_hashtable_[i]->find_hotree(id, p1, p2);
//             // }
//         }
//     }
//     return result_branch;
// }

// 【修改】Retrieve 函数
Branch* HOTree::Retrieve(Client* client_, Triple*& triple, std::shared_lock<std::shared_mutex>& read_lock, int user_id) {
    Branch* child_branch = nullptr;
    int id = triple->id;
    int level_i = triple->level;

    // ---------------------------------------------------------
    // 1. ID 检测机制：检查所有用户的 Stash (Cross-Stash Check)
    // ---------------------------------------------------------
    // 这是一个读操作，需要锁，但因为只是 map 查找，速度很快
    for(int i = 0; i < client_->num_users_; ++i) {
        std::lock_guard<std::mutex> lg(*client_->user_stash_mtxs_[i]);
        auto& target_stash = client_->user_stashes_[i];
        auto it = target_stash.find(id);
        if(it != target_stash.end()) {
            child_branch = it->second;
            // 找到了！直接使用，不需要把数据移动到当前用户的 stash
            // 保持它在原来的位置即可，因为 stash 只是暂存区
            break; 
        }
    }

    // ---------------------------------------------------------
    // 2. 检查 Level Stash (这部分逻辑保持不变)
    // ---------------------------------------------------------
    if(child_branch == nullptr) {
        std::lock_guard<std::mutex> lg(*client_->level_stash_mtxs_[level_i]);
        auto& level_stash = client_->vector_every_level_stash_[level_i];
        auto it = std::find_if(level_stash.begin(), level_stash.end(), [&](Branch* b) {
            return b != nullptr && b->id == id && b->counter_for_lastest_data == triple->counter_for_lastest_data;
        });
        if (it != level_stash.end()) {
            child_branch = *it;
            // level_stash.erase(it); // 建议此处不要 erase，等 Eviction 统一清空，或者加更复杂的锁
        }
    }

    // ---------------------------------------------------------
    // 3. ORAM Access (如果没有在任何 Stash 中找到)
    // ---------------------------------------------------------
    if(child_branch == nullptr) {
        child_branch = Access(id, triple->counter_for_lastest_data, level_i, user_id);
        if(child_branch) {
            child_branch->trueData = client_->cryptor_->aes_decrypt(child_branch->trueData, level_i);
        } else {
            child_branch = Self_healing_Access(id, triple->counter_for_lastest_data, level_i, user_id);
        }
    }

    // 更新元数据
    child_branch->level = -1;
    triple->level = client_->get_first_empty_level();
    triple->counter_for_lastest_data++;
    child_branch->counter_for_lastest_data = triple->counter_for_lastest_data;

    // ---------------------------------------------------------
    // 4. 写入当前用户的 Stash
    // ---------------------------------------------------------
    bool inserted_new = false;
    
    // 如果刚才是在别人的 stash 里找到的，我们是否要移动到自己的 stash？
    // 为了简化逻辑和减少锁竞争，通常策略是：
    // 如果已经在某个 stash 里了，就不用动了（update in place）。
    // 如果是新从 Tree 里拿出来的，才放入 current user stash。
    
    // 简单起见，我们检查它是否已经在任何 stash 里（上面的步骤1已经查过了）
    // 但为了严谨，我们需要确定 child_branch 是否已经在 map 中。
    // 这里采用简单策略：只有当 child_branch 不在任何 stash 时才插入。
    // 由于我们是指针操作，如果步骤1找到了，child_branch 就是那个指针，它已经在 map 里了。
    
    // 我们需要一个标记来判断是否需要 insert
    bool already_in_stash = false;
    // (实际上，可以通过检查 child_branch 的来源判断，但为了代码简洁，这里做个简化假设：
    // 如果是从 Access() 返回的，或者还没有 owner，则加入)

    // 更高效的做法：再次检查当前用户 stash (双重检查) 或者 只要是指针地址没变就不管
    // 这里演示最标准的：只有新获取的数据才放入自己的 stash
    
    // 检查这个指针是否已经在当前用户的 stash 里（避免重复计数）
    {
        std::lock_guard<std::mutex> lg(*client_->user_stash_mtxs_[user_id]);
        if(client_->user_stashes_[user_id].find(id) == client_->user_stashes_[user_id].end()) {
            // 如果它也不在其他用户的 stash 里（这需要比较复杂的判断，或者我们就允许少量冗余）
            // 既然用户建议“已经在其他stash中就不查了”，说明这里我们可以认为：
            // 如果步骤1找到了，就不执行这里的插入。
            
            // 怎么判断步骤1是否找到？
            // 可以加一个 flag。让我们优化一下逻辑流。
        }
    }
    
    // --- 修正后的写入逻辑 ---
    // 我们可以在步骤1加一个 flag `found_in_stash`
    // if (!found_in_stash) { ... Access ...;  Lock(user_stash); insert; count++; }
    
    // 假设上面加了 flag，这里执行写入：
    // 只有当它是从 Access 新拿出来的，才放入当前用户的 stash
    // 这里的判断条件略微复杂，为了演示，假设我们需要插入：
    {
         std::lock_guard<std::mutex> lg(*client_->user_stash_mtxs_[user_id]);
         // 只有当 ID 不存在时才插入
         if (client_->user_stashes_[user_id].find(id) == client_->user_stashes_[user_id].end()) {
             // 还需要再遍历一次其他 stash 确认吗？不用，因为如果步骤1没找到， Access 后它就是新的。
             // 但并发环境下，可能别人刚才插入了。为了绝对一致性，可以允许少量重复，或者再次检查。
             // 这里选择：允许放入当前 stash，最后 atomic count 增加。
             
             client_->user_stashes_[user_id][id] = child_branch;
             inserted_new = true;
         }
         // 如果已经在自己的 stash 里，直接 update (Branch 是指针，内容已更，无需 map 操作)
    }

    // 5. 触发全局驱逐
    bool needs_eviction = false;
    if (inserted_new) {
        int total = client_->current_total_stash_size_.fetch_add(1) + 1;
        if (total >= Z) {
            needs_eviction = true;
        }
    }

    if (needs_eviction) {
        read_lock.unlock(); // 释放读锁
        {
            std::unique_lock<std::shared_mutex> write_lock(rw_mutex_);
            if (client_->current_total_stash_size_.load() >= Z) {
                Eviction(client_);
                client_->current_total_stash_size_.store(0);
            }
        }
        read_lock.lock(); // 恢复读锁
    }

    return child_branch;
}

// Branch* HOTree::Retrieve(Client* client_, Triple*& triple) {
    
//     Branch* child_branch = nullptr;
//     // 将要取回的id和该id所在的层的预测
//     int level_i = triple->level;
//     int id = triple->id;
//     if(if_is_debug) {
//         cout<<"We will retrieve id: "<<id<<endl;
//     }
    
//     int counter_for_lastest_data = triple->counter_for_lastest_data;
    
//     /*----------------------------------Find data in Client-----------------------------------------*/
//     if(child_branch == nullptr) { // found in client stash
//         auto it = client_->stash_.find(id);
//         if(it != client_->stash_.end()) {
//             child_branch = it->second;
//         }
//     }

//     // lookup cuckoo stash to find the id if data is not in hash table. If found, delete from cuckoo stash and move to stash
//     if(child_branch == nullptr) {
//         auto& level_stash = client_->vector_every_level_stash_[level_i];
//         auto it = std::find_if(level_stash.begin(), level_stash.end(), [&](Branch* b) {
//             return b != nullptr && b->id == id && b->counter_for_lastest_data == counter_for_lastest_data;
//         });
//         if (it != level_stash.end()) {
//             child_branch = *it; 
//             level_stash.erase(it);
//         }
//     }

//     /*----------------------------------Find data in Server cuckoo tables-----------------------------------------*/
//     if(child_branch == nullptr) {
//         // child_branch is not nullptr if prediction is right.
//         child_branch = Access(id, counter_for_lastest_data, level_i);
//         if(child_branch!=nullptr) {
//             // if getting the data, decryt it and update its status
//             child_branch->trueData = client_->cryptor_->aes_decrypt(child_branch->trueData, level_i); // decrypt the data using secret key in level i
//         }
//         else { // if not found, target id must be the level gerter than prediction level
//             child_branch = Self_healing_Access(id, counter_for_lastest_data, level_i);
//         }
//     }

//     /*----------------------------------update prediction values-----------------------------------------*/
//     // 取回数据后，不管是从本地还是server上取的，都已经确定访问了id一次，且取回来了，因此counter++，
//     child_branch->level = -1;
//     client_->stash_[id] = child_branch; // move the data to stash
//     triple->level = client_->get_first_empty_level();
//     if(id == debug_id && if_is_debug) {
//         cout<<"id "<< id <<" counter before updating: "<< child_branch->counter_for_lastest_data <<endl;
//     }
//     triple->counter_for_lastest_data++;
//     child_branch->counter_for_lastest_data = triple->counter_for_lastest_data;
//     if(id == debug_id && if_is_debug) {
//         cout<<"id "<< id <<" counter have update: "<< child_branch->counter_for_lastest_data <<endl;
//     }
//     if(client_->stash_.size() % Z == 0 && client_->stash_.size() != 0) {
//         Eviction(client_); 
//     }
//     return child_branch;
// }

// 定义一个新的搜索项结构体，用于优先队列
struct LazySearchItem {
    double score;
    Triple* triple_info; // 存储要去哪里取数据
    Branch* fetched_branch; // 如果已经取回来了（比如根节点），存这里；否则为 nullptr

    // 优先队列需要重载 < 运算符 (大顶堆，分数小的在下，但通常我们要TopK大的，或者根据距离小的)
    // 假设是相似度越高越好
    bool operator<(const LazySearchItem& other) const {
        return score < other.score; 
    }
};


vector<pair<double, DataRecord>> HOTree::SearchTopK(double qx, double qy, string qText, int k, Client* client, int user_id) {
    std::shared_lock<std::shared_mutex> read_lock(rw_mutex_);
    vector<pair<double, DataRecord>> results;
    if (root.size() == 0 || k <= 0) return results;

    // 1. 构建查询节点
    Branch* queryBranch = new Branch();
    queryBranch->m_rect.min_Rec[0] = queryBranch->m_rect.max_Rec[0] = qx;
    queryBranch->m_rect.min_Rec[1] = queryBranch->m_rect.max_Rec[1] = qy;
    queryBranch->CalcuKeyWordWeight(qText, dic_str);
    queryBranch->level = -1;

    priority_queue<LazySearchItem> pq;
    double min_score = -1.0; 

    // 2. 初始层入队
    // 因为我们在 Build 里已经强制合并为单根(或少根)，这里 Retrieve 次数很少
    for (auto & triple : root) {
        // 根节点必须取回，无法避免
        auto child_branch = Retrieve(client_, triple, read_lock, user_id);
        
        
        // 计算根节点的分数
        double score = client_->CalcuTestSPaceRele(child_branch, queryBranch);
        
        // 根节点已经取回了，放入 fetched_branch
        pq.push({score, triple, child_branch}); 
        
    }
    
    // 3. 循环搜索
    while (!pq.empty() && results.size() < k) {
        LazySearchItem top = pq.top();
        pq.pop(); 

        // 剪枝
        if (results.size() >= k && top.score < min_score) {
            break; 
        }

        Branch* curr = top.fetched_branch;

        // [关键逻辑改变]：如果 curr 为空，说明这是一个待访问的孩子，我们需要现在去取它 (Retrieve)
        if (curr == nullptr) {
            // 产生 1 次 ORAM IO
            curr = Retrieve(client_, top.triple_info, read_lock, user_id);
            
            // 注意：取回后不需要再算分了，因为 top.score 已经是我们用父节点摘要算出来的准确分数
        }

        // 检查是否为数据节点 (假设 id >= 0 是数据， < 0 是中间节点)
        // 注意：根据你的代码逻辑，叶子节点的 id 似乎是 >= 0 的
        if (curr->child_triple.empty() || curr->id >= 0) { 
            // 找到结果
            results.push_back(make_pair(top.score, id_to_record_vec[curr->id]));
            if (results.size() >= k) {
                min_score = results.back().first;
            }
        } else {
            // 是中间节点：展开子节点
            // [核心优化]：这里不再 Retrieve 孩子，而是利用 curr 中的摘要信息算分
            
            // 确保 Build 阶段正确填充了 child_rects 和 child_weights_vec
            // 且大小与 child_triple 一致
            size_t child_count = curr->child_triple.size();
            
            for (size_t i = 0; i < child_count; i++) {
                Triple* child_t = curr->child_triple[i];
                
                // --- 模拟计算分数 (不进行 Retrieve) ---
                // 我们需要一个临时的 Branch 对象来辅助计算，或者重载 CalcuTestSPaceRele
                // 这里创建一个轻量级的 "Stub" Branch
                Branch* stub = new Branch();
                stub->m_rect = curr->child_rects[i];         // 从父节点拿矩形
                stub->weight = curr->child_weights_vec[i];   // 从父节点拿权重
                // 注意：stub->text 没有，但 weight 已经有了，CalcuTestSPaceRele 应该使用 weight
                
                // 计算分数 (纯内存操作，无 IO)
                double score = client_->CalcuTestSPaceRele(stub, queryBranch);
                
                delete stub; // 用完即弃

                // 将 {分数, 目标ID, nullptr} 推入队列
                // nullptr 表示"还没取回"，等它浮动到堆顶时再取
                pq.push({score, child_t, nullptr});
            }
            
            // 当前节点处理完毕，如果内存满了需要驱逐
            
        }
        
        // 如果 top.fetched_branch 不为空（比如根节点），这里不需要 delete，因为 Retrieve 返回的指针可能在 stash 中管理
        // 但根据 Retrieve 的逻辑，它似乎返回一个 new 的对象。如果该对象被放入 stash，client 会管理。
        // 此处逻辑需根据 Retrieve 的内存管理策略微调。
    }
    delete queryBranch;
    return results;
}


void HOTree::Build(vector<DataRecord>& raw_data, Client* &client) {
    // initial Client parameters using stash size Z and size of dataset N
    if (raw_data.empty()) return;

    id_to_record_vec = std::move(raw_data); 
    vector<Branch*> position_branchs;
    int temp_ID = 0;

    // 1. 数据转换为 Branch
    for (const auto& data : id_to_record_vec) {
        Branch* mBranch = new Branch();
        mBranch->is_empty_data = false;
        mBranch->id = data.id;
        mBranch->text = data.processed_text;
        // mBranch->trueData = client_->cryptor_->aes_encrypt(padZero(data.processed_text), L); // ciphertext, it has been padded to blocksize before encrypting
        
        mBranch->CalcuKeyWordWeight(mBranch->text, dic_str); //当前使用的是叶子节点，因此，他需要跟每个关键词计算相似度

        mBranch->m_rect.min_Rec[0] = mBranch->m_rect.max_Rec[0] = data.x_coord;
        mBranch->m_rect.min_Rec[1] = mBranch->m_rect.max_Rec[1] = data.y_coord;
        // mBranch->pointBranch = mBranch;
        // mBranch->curNode = nullptr;

        // append the leaf branch to vector
        all_branchs.push_back(mBranch);
        position_branchs.push_back(mBranch);
        temp_ID++;
    }
    int data_num = position_branchs.size();

    // 3. STR 构建
    vector<Branch*> Branchs_at_IR_tree;
    
    // X 轴排序
    sort(position_branchs.begin(), position_branchs.begin() + data_num, 
         [](Branch* a, Branch* b) { return a->m_rect.min_Rec[0] < b->m_rect.min_Rec[0]; });

    int current_idx = 0;
    int non_leaf_branch_id = -1;
    while (current_idx < position_branchs.size()) {//每次处理MAX_SIZE个数据
        int end_idx = min((size_t)(current_idx + MAX_SIZE), position_branchs.size());
        
        // Y 轴局部排序
        if (current_idx < data_num) {
             int sort_end = min(end_idx, data_num);
             sort(position_branchs.begin() + current_idx, position_branchs.begin() + sort_end,
                  [](Branch* a, Branch* b) { return a->m_rect.min_Rec[1] < b->m_rect.min_Rec[1]; });
        }

        Branch* parent_branch = new Branch();
        if(non_leaf_branch_id == -229 && if_is_debug) {
            cout<<"1";
        }
        parent_branch->id = non_leaf_branch_id--;
        parent_branch->initRectangle(); // 初始化矩形
        //MAX_SIZE个数据放在一个节点m_node中，这个node包含这些branch的矩形
        for (int i = current_idx; i < end_idx; i++) {
            Branch* b = position_branchs[i];
            Triple* temp_triple = new Triple(b->id, 0, 0);
            parent_branch->child_triple.push_back(temp_triple);
            parent_branch->child_branch.push_back(b);
            parent_branch->rectUpdate(b); 
            parent_branch->child_rects.push_back(b->m_rect);
            parent_branch->child_weights_vec.push_back(b->weight);
        }
        current_idx = end_idx;
        Branchs_at_IR_tree.push_back(parent_branch);
        all_branchs.push_back(parent_branch);
    }

    // 向上构建，构建父节点
    while (Branchs_at_IR_tree.size() > MAX_SIZE) {
        vector<Branch*> parent_branchs; //记录当前层的节点向量
        int branch_idx = 0;
        
        while (branch_idx < Branchs_at_IR_tree.size()) {
            Branch* parent = new Branch(); //一个节点存MAX_SIZE个branch
            parent->initRectangle();
            parent->id = non_leaf_branch_id--;
            int pack_count = 0;

            while (pack_count < MAX_SIZE && branch_idx < Branchs_at_IR_tree.size()) { //遍历孩子节点，每MAX_SIZE变成一个新的branch
                Branch* childBranch = Branchs_at_IR_tree[branch_idx];
                // 聚合权重
                childBranch->weight.resize(dic_str.size(), 0.0);
                parent->keyWeightUpdate(childBranch);
                // record parent infomation
                parent->rectUpdate(childBranch); // 更新父节点 MBR
                Triple* temp_triple = new Triple(childBranch->id, 0, 0);
                parent->child_triple.push_back(temp_triple);
                parent->child_branch.push_back(childBranch);

                // [修改点2] 关键：将孩子的摘要信息保存到父节点中
                parent->child_rects.push_back(childBranch->m_rect);
                // 确保 childBranch->weight 已经被正确计算或初始化
                parent->child_weights_vec.push_back(childBranch->weight);

                // append the non-leaf branch to vector
                branch_idx++;
                pack_count++;
            }
            parent_branchs.push_back(parent);
            all_branchs.push_back(parent);
        }
        Branchs_at_IR_tree = parent_branchs;
    }
    if (!Branchs_at_IR_tree.empty()) {
        for(auto & b : Branchs_at_IR_tree) {
            Triple* root_triple = new Triple(b->id, 0, 0);
            root.push_back(root_triple);
            // all_branchs.push_back(b);
        }
    }

    int N = all_branchs.size();
    int l = ceil(log2(Z));   //the lowest level
    int L = ceil(log2(N)); //the top level
    for(auto & triple : root) {
        triple->level = L;
    }
    client_ = new Client(L);
    client_->min_level_ = l;
    client_->max_level_ = L;
    for(int level_i = 0; level_i <= L; level_i++) {
        if (level_i < l) {
            // 0 到 l-1 层填入空指针
            vec_hashtable_.push_back(nullptr);
        } else {
            // l 到 L 层构建真实对象
            // make_unique 会动态分配内存并返回指针
            size_t size = pow(2, level_i);
            vec_hashtable_.push_back(make_unique<CuckooTable>(size, level_i));
        }
    }
    
    for(auto & branch : all_branchs) {
        // update the branch itself information
        branch->level = L;
        branch->trueData = client_->cryptor_->aes_encrypt(padZero(branch->text), L); // ciphertext, it has been padded to blocksize before encrypting
        // update the child level
        for(auto& triple : branch->child_triple) {
            triple->level = L;
        }
        
        // insert
        vec_hashtable_[L]->insert(branch, client_);
    }

    for(int i = 0; i < vec_hashtable_[L]->stash.size(); i++) {
        // move the stash to client, stash is so small that client is easy to save
        Branch* temp_branch = vec_hashtable_[L]->stash[i];
        temp_branch->trueData = client_->cryptor_->aes_decrypt(temp_branch->trueData, L);
        client_->vector_every_level_stash_[L].push_back(temp_branch);
    }
    // vec_hashtable_[L]->stash.clear();
    client_->vec_hotree_level_i_is_empty_[L] = false;
    // printf("Initially status is as following:");
    string temp = "After Oblivious Shuffle & Insert level: "+L;
    vec_hashtable_[L]->print_table_status(temp, client_->vector_every_level_stash_[L]);

}