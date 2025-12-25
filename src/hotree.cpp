#include "hotree.h"
using namespace std;

void HOTree::print_stash() {
    for(auto const& [key, val] : client_->stash_) {
        std::cout << "Key: " << key << std::endl;
    }
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
    // 既然 all_branchs 已经保存了所有的 Branch，包括根、中间、叶子
    // 我们只需要把这张清单上的东西全部销毁即可，不需要管它们是什么关系
    for (auto* b : all_branchs) {
        if (b) {
            // 先处理 Branch 内部的 Triple 碎屑
            for (auto* t : b->child_triple) delete t;
            // 释放 Branch
            delete b;
        }
    }
    
    // 最后清理 root 里的 Triple 指针
    for (auto* t : root) delete t;
    
    // 释放 Client
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
    vector<Branch*> all_shuffled_branchs;
    vector<int> branchs_level_belong_to;
    int target_level = client_->get_first_empty_level();
    cout<<"---------------------------------------start shuffle---------------------------------------\n";

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

            // move the data in cuckoo table stash to a vector
            for(auto & elem : client_->vector_every_level_stash_[level_i]) {
                elem->level = target_level;
                all_shuffled_branchs.push_back(elem);
                branchs_level_belong_to.push_back(level_i);
            }
            
            // if data have removed, clear hash table
            client->vec_hotree_level_i_is_empty_[level_i] = true; // level_i will be empty
            client_->vector_every_level_stash_[level_i].clear();
            cout<<"level "<<level_i<<" is no empty"<<endl;
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
        // if data have removed, clear hash table
        client_->vector_every_level_stash_[target_level].clear();
    }
    client->vec_hotree_level_i_is_empty_[target_level] = false; // level_i will be full

    /*-------------------------Move data of client stash to a vector-------------------------------*/
    for (auto const& [id, elem] : client->stash_) {
        elem->trueData = client->cryptor_->aes_encrypt(elem->trueData, target_level);
        elem->level = target_level;
        if(elem->id == debug_id) {
            printf("id {%d} is in stash with counter {%d} \n", elem->id, elem->counter_for_lastest_data);
        }
        all_shuffled_branchs.push_back(elem);
        branchs_level_belong_to.push_back(target_level);
    }
    client->stash_.clear();

    /*-------------------------update prediction level of all data-------------------------------*/
    for(auto & branch : all_shuffled_branchs) {
        for(auto & triple : branch->child_triple) {
            if(triple->level < target_level) {
                triple->level = target_level;
            }
        }
        if(branch->id == debug_id) {
            printf("id %d exist with counter %d in hotree.cpp\n", branch->id, branch->counter_for_lastest_data);
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

    cout<< all_shuffled_branchs.size() <<" data is shuffled and moved to level: "<<target_level<<endl;
    cout<<"---------------------------------------end shuffle---------------------------------------\n";
}

//access some level with exactly level_i and access other levels with dummy
Branch* HOTree::Access(uint64_t id, int counter_for_lastest_data, int level_i) {
    uint64_t id_counter_combine = combine_unique(id, counter_for_lastest_data);
    Branch* result_branch = nullptr;
    
    client_->communication_round_trip_ += 0.5; // only one trip

    // access every level if the client stash have not found target id data
    for(int i = client_->min_level_; i < client_->vec_hotree_level_i_is_empty_.size(); i++) {
        if(client_->vec_hotree_level_i_is_empty_[i]) {
            continue; // empty level pass
        }
        else {
            if(i == level_i) {
                size_t p1 = client_->compute_hash1(id_counter_combine, i, vec_hashtable_[i]->getTableCapacity());
                size_t p2 = client_->compute_hash2(id_counter_combine, i, vec_hashtable_[i]->getTableCapacity());

                if(id == debug_id) {
                    std::cout<<"In search level"<< level_i <<" p1: "<<p1 << " seed: "<<client_->vec_seed1_[level_i]<<" table size"<< vec_hashtable_[i]->getTableCapacity()<<std::endl;
                }

                auto vec_temp_branch = vec_hashtable_[i]->find_hotree(id, p1, p2);
                client_->communication_volume_ += BlockSize*2; // two blocks
                for(auto& elem : vec_temp_branch) {
                    if(elem != nullptr) {
                        if(elem->id == id && elem->counter_for_lastest_data == counter_for_lastest_data) {
                            result_branch = new Branch(elem);
                        }
                    }   
                }
            }
            else {
                size_t p1 = client_->getRandomIndex(vec_hashtable_[i]->getTableCapacity());
                size_t p2 = client_->getRandomIndex(vec_hashtable_[i]->getTableCapacity());
                client_->communication_volume_ += BlockSize*2; // two blocks
                auto vec_temp_branch = vec_hashtable_[i]->find_hotree(id, p1, p2);
            }
        }
    }
    return result_branch;
}

Branch* HOTree::Self_healing_Access(int id, int counter_for_lastest_data) {
    Branch* result_branch = nullptr;
    client_->communication_round_trip_ += 0.5; // only half trip, no need to put back

    /*--------------------------------find the data using standard Hierarchical ORAM, compute index using real id untill found---------------------*/
    for(int i = client_->min_level_; i < client_->vec_hotree_level_i_is_empty_.size(); i++) {
        if(client_->vec_hotree_level_i_is_empty_[i]) {
            continue; // empty level pass
        }
        else {
            // lookup cuckoo stash to find the id if data is not in hash table. If found, delete from cuckoo stash and move to stash
            if(result_branch == nullptr) {
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

    /*--------------------------------find the data on server tables---------------------*/
    for(int i = client_->min_level_; i < client_->vec_hotree_level_i_is_empty_.size(); i++) {
        if(client_->vec_hotree_level_i_is_empty_[i]) {
            continue; // empty level pass
        }
        else {
            if(result_branch == nullptr) {
                size_t p1 = client_->compute_hash1(combine_unique(id, counter_for_lastest_data), i, vec_hashtable_[i]->getTableCapacity());
                size_t p2 = client_->compute_hash2(combine_unique(id, counter_for_lastest_data), i, vec_hashtable_[i]->getTableCapacity());

                if(id == debug_id) {
                    std::cout<<"In self heal search level"<< i <<" p1: "<<p1 << " seed: "<<client_->vec_seed1_[i]<<" table size"<< vec_hashtable_[i]->getTableCapacity()<<std::endl;
                }
                auto vec_temp_branch = vec_hashtable_[i]->find_hotree(id, p1, p2);
                client_->communication_volume_ += BlockSize*2; // two blocks
                for(auto& elem : vec_temp_branch) {
                    if(elem != nullptr) {
                        elem->trueData = client_->cryptor_->aes_decrypt(elem->trueData, i);
                        if(elem->id == id && elem->counter_for_lastest_data == counter_for_lastest_data) {
                            result_branch = new Branch(elem);
                        }
                    }   
                }
            }
            else { //dummy lookup but no need to decrypt beacause data have found
                size_t p1 = client_->getRandomIndex(vec_hashtable_[i]->getTableCapacity());
                size_t p2 = client_->getRandomIndex(vec_hashtable_[i]->getTableCapacity());
                client_->communication_volume_ += BlockSize*2; // two blocks
                auto vec_temp_branch = vec_hashtable_[i]->find_hotree(id, p1, p2);
            }
        }
    }
    return result_branch;
}

Branch* HOTree::Retrieve(Client* client_, Triple*& triple) {
    Branch* child_branch = nullptr;
    // 将要取回的id和该id所在的层的预测
    int level_i = triple->level;
    int id = triple->id;
    int counter_for_lastest_data = triple->counter_for_lastest_data;
    
    /*----------------------------------Find data in Client-----------------------------------------*/
    if(child_branch == nullptr) { // found in client stash
        auto it = client_->stash_.find(id);
        if(it != client_->stash_.end()) {
            child_branch = it->second;
        }
    }

    // lookup cuckoo stash to find the id if data is not in hash table. If found, delete from cuckoo stash and move to stash
    if(child_branch == nullptr) {
        auto& level_stash = client_->vector_every_level_stash_[level_i];
        auto it = std::find_if(level_stash.begin(), level_stash.end(), [&](Branch* b) {
            return b != nullptr && b->id == id;
        });
        if (it != level_stash.end()) {
            child_branch = *it; 
            level_stash.erase(it);
        }
    }

    /*----------------------------------Find data in Server cuckoo tables-----------------------------------------*/
    if(child_branch == nullptr) {
        // child_branch is not nullptr if prediction is right.
        child_branch = Access(id, counter_for_lastest_data, level_i);
        if(child_branch!=nullptr) {
            // if getting the data, decryt it and update its status
            child_branch->trueData = client_->cryptor_->aes_decrypt(child_branch->trueData, level_i); // decrypt the data using secret key in level i
        }
        else { // if not found, target id must be the level gerter than prediction level
            child_branch = Self_healing_Access(id, counter_for_lastest_data);
        }
    }

    /*----------------------------------update prediction values-----------------------------------------*/
    // 取回数据后，不管是从本地还是server上取的，都已经确定访问了id一次，且取回来了，因此counter++，
    child_branch->level = -1;
    client_->stash_[id] = child_branch; // move the data to stash
    triple->level = client_->get_first_empty_level();
    if(id == debug_id) {
        cout<<"id "<< id <<" counter before updating: "<< child_branch->counter_for_lastest_data <<endl;
    }
    triple->counter_for_lastest_data++;
    child_branch->counter_for_lastest_data = triple->counter_for_lastest_data;
    if(id == debug_id) {
        cout<<"id "<< id <<" counter have update: "<< child_branch->counter_for_lastest_data <<endl;
    }
    return child_branch;
}

vector<pair<double, DataRecord>> HOTree::SearchTopK(double qx, double qy, string qText, int k, Client* client) {
    vector<pair<double, DataRecord>> results;
    if (root.size() == 0 || k <= 0) return results;

    // 1. 构建查询节点
    Branch* queryBranch = new Branch();
    queryBranch->m_rect.min_Rec[0] = queryBranch->m_rect.max_Rec[0] = qx;
    queryBranch->m_rect.min_Rec[1] = queryBranch->m_rect.max_Rec[1] = qy;
    queryBranch->CalcuKeyWordWeight(qText, dic_str);
    queryBranch->level = -1;

    // 2. 使用 priority_queue 替代 vector+sort
    priority_queue<SearchItem> pq;
    double min_score = -1.0; 

    // 3. 初始层入队
    for (auto & triple : root) {
        // 确定triple->id是要取的，那么先取回，然后更新triple
        auto child_branch = Retrieve(client_, triple); //更新操作已经在Retrieve()里做了
        
        double score = client_->CalcuTestSPaceRele(child_branch, queryBranch);
        pq.push({score, child_branch});
        // if the client memory is full, it will be removed to the first empty level in HOTree
        if(client_->stash_.size() % Z == 0 && client_->stash_.size() != 0) {
            Eviction(client_);
        }
    }
    
    // 4. 循环搜索
    while (!pq.empty() && results.size() < k) {
        SearchItem top = pq.top();
        pq.pop(); // O(log N) 操作，比 erase 高效得多

        // 【核心剪枝 1：出队截断】
        // 如果当前最大的分数已经小于当前的门槛分数，说明后面所有的节点都不可能入选
        if (results.size() >= k && top.score < min_score) {
            break; // 直接终止 while 循环，不需要清理 pq，因为后续元素只会更小
        }

        Branch* curr = top.branch;

        // 【关键逻辑】检查是否已经到达叶子节点
        if (curr->id >= 0) { // id < 0 represents non-leaf branch
            results.push_back(make_pair(top.score, id_to_record_vec[curr->id]));
            if (results.size() >= k) {
                min_score = results.back().first;
            }
        } else {
            // 是中间节点：展开子节点
            vector<Triple*> vec_child_triples = curr->child_triple;
            if (vec_child_triples.size() > 0) {
                for (auto* triple : vec_child_triples) {
                    // It is for true, triple.id will be accessed. So we change its level in advance
                    
                    if(curr->level > triple->level) {
                        printf("curr id %d (in level %d) have child id %d in level %d \n", curr->id, curr->level, triple->id, triple->level);
                    }
                    auto child_branch = Retrieve(client_, triple);

                    double score = client_->CalcuTestSPaceRele(child_branch, queryBranch);
                    pq.push({score, child_branch});

                    // if the client memory is full, it will be removed to the first empty level in HOTree
                    if(client_->stash_.size() % Z == 0 && client_->stash_.size() != 0) {
                        Eviction(client_); 
                    }
                }
            }
        }
    }
    delete queryBranch;
    return results;
}

void HOTree::Build(vector<DataRecord>& raw_data, Client* &client) {
    // initial Client parameters using stash size Z and size of dataset N
    if (raw_data.empty()) return;
    
    vector<Branch*> all_branchs; //record the all branch, including both leaf nodes and non-leaf nodes

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
        parent_branch->id = non_leaf_branch_id--;
        parent_branch->initRectangle(); // 初始化矩形
        //MAX_SIZE个数据放在一个节点m_node中，这个node包含这些branch的矩形
        for (int i = current_idx; i < end_idx; i++) {
            Branch* b = position_branchs[i];
            Triple* temp_triple = new Triple(b->id, 0, 0);
            parent_branch->child_triple.push_back(temp_triple);
            parent_branch->child_branch.push_back(b);
            parent_branch->rectUpdate(b); 
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
            all_branchs.push_back(b);
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
    vec_hashtable_[L]->stash.clear();
    client_->vec_hotree_level_i_is_empty_[L] = false; 
}