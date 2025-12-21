#include "define.h"
#include "tree.h"
#include "DataReader.h"
#include "cryptor.h"
#include "client.h"
#include "OHT.h" // cuckoo table
#include "hotree.h"

using namespace std;

HOTree::HOTree(const vector<string>& dict){
    // 初始化全局变量
    vec_hashtable_.clear();
    dic_str = dict;
    dic_map.clear();
    id_to_record_map.clear();
    for (size_t i = 0; i < dict.size(); ++i) {
        dic_map[dict[i]] = (int)i;
    }
}

// 递归删除函数：处理 Node -> Branch -> child Node 的深度遍历
void HOTree::DeleteNodeRecursive(Node* node) {
    if (node == nullptr) return;

    // 1. 遍历当前节点的所有 Branch
    for (Branch* b : node->mBranch) {
        if (b != nullptr) {
            // 2. 如果 Branch 指向子节点，递归进去
            if (b->childNode != nullptr) {
                DeleteNodeRecursive(b->childNode);
                b->childNode = nullptr; 
            }
            // 3. 删除 Branch 本身（因为 Build 中使用了 new Branch()）
            delete b;
        }
    }
    node->mBranch.clear();

    // 4. 删除节点本身
    delete node;
}

HOTree::~HOTree() {
    // 1. 安全地递归删除树结构（Node 和 Branch）
    if (root != nullptr) {
        DeleteNodeRecursive(root);
        root = nullptr;
    }

    // 2. 处理 Client 指针
    // 在 Build 函数中执行了 client_ = new Client(L)，所以这里必须 delete
    if (client_ != nullptr) {
        delete client_;
        client_ = nullptr;
    }

    // 3. 清理映射表
    id_to_record_map.clear();
    dic_map.clear();

    // 4. 处理智能指针容器
    // 虽然 unique_ptr 会在 vector 销毁时自动释放，但 clear() 可以明确释放顺序
    vec_hashtable_.clear();
}

Client* HOTree::getClient() {
    return client_;
}
/* 
When client memory is full, merge this memory and all upper level data(until the first empty level).
Merging is non-oblivious, because we use oblivous shuffle to confuse the all queried data after merging
*/
void HOTree::Eviction(Client* client) {
    vector<Branch> all_shuffled_branchs;
    int target_level;
    
    for(int level_i = client_->min_level_; level_i < vec_hashtable_.size(); level_i++) {
        if(!client->vec_hotree_level_i_is_empty_[level_i]) { // level_i is not empty
            // move the data in cuckoo table to a vector
            for(auto & entry : vec_hashtable_[level_i]->table) {
                if(entry.occupied) {
                    entry.branch.trueData = client->cryptor_->aes_decrypt(entry.branch.trueData, level_i);
                    all_shuffled_branchs.push_back(entry.branch);
                }
            }
            // move the data in cuckoo table stash to a vector
            for(auto & elem : client_->vector_every_level_stash_[level_i]) {
                all_shuffled_branchs.push_back(elem);
            }
        }
        else {
            target_level = level_i; // mark the level id that all data will be removed to
            break;
        }
    }
    for(auto & elem : client->stash_) {
        elem.trueData = client->cryptor_->aes_encrypt(elem.trueData, target_level);
        all_shuffled_branchs.push_back(elem);
    }
    vec_hashtable_[target_level]->oblivious_shuffle_and_insert(all_shuffled_branchs, client); // this fuction includes updating hash seed.
}

//access some level with exactly level_i and access other levels with dummy
Branch* HOTree::Access(int id, int level_i) {
    Branch* result_branch = nullptr;
    for(auto & elem : client_->stash_) {
        if(elem.id == id) {
            return &elem; // if found in local stash, return
        }
    }
    client_->communication_round_trip_ ++; // only one trip

    // access every level if the client stash have not found target id data
    for(int i = client_->min_level_; i < client_->vec_hotree_level_i_is_empty_.size(); i++) {
        if(client_->vec_hotree_level_i_is_empty_[i]) {
            // std::cout << "Debug: Skipping level " << i << " because it is marked EMPTY" << std::endl;
            continue; // empty level pass
        }
        else {
            if(i == level_i) {
                size_t p1 = client_->compute_hash1(id, i, vec_hashtable_[i]->getTableCapacity());
                size_t p2 = client_->compute_hash2(id, i, vec_hashtable_[i]->getTableCapacity());
                // if(id == -201) {
                //     std::cout<<"In access level"<< i <<" p1: "<<p1 << " p2 :"<<p2<<" seed: "<<client_->vec_seed1_[11]<<" table size"<< vec_hashtable_[i]->size()<<std::endl;
                // }
                auto vec_temp_branch = vec_hashtable_[i]->find_hotree(id, p1, p2);
                client_->communication_volume_ += BlockSize*2; // two blocks
                for(auto& elem : vec_temp_branch) {
                    if(elem->id == id) {
                        result_branch = elem;
                    }
                }
                // lookup cuckoo stash to find the id if data is not in hash table. If found, delete from cuckoo stash
                auto& level_stash = client_->vector_every_level_stash_[i];
                auto it = std::find_if(level_stash.begin(), level_stash.end(), [&](const Branch& b) {
                    return b.id == id;
                });

                if (it != level_stash.end()) {
                    result_branch = &(*it); // 获取容器内对象的地址
                    // 注意：如果你在这里删除了元素（erase），result_branch 会失效
                    // 建议等处理完 trueData 后再考虑是否删除
                }
            }
            else {
                size_t p1 =client_->getRandomIndex(vec_hashtable_[i]->size());
                size_t p2 =client_->getRandomIndex(vec_hashtable_[i]->size());
                client_->communication_volume_ += BlockSize*2; // two blocks
                auto vec_temp_branch = vec_hashtable_[i]->find_hotree(id, p1, p2);
            }
        }
    }
    return result_branch;
}

vector<pair<double, DataRecord>> HOTree::SearchTopK(double qx, double qy, string qText, int k, Client* client) {
    vector<pair<double, DataRecord>> results;
    if (!root || k <= 0) return results;

    // 1. 构建查询节点
    Branch* queryBranch = new Branch();
    queryBranch->m_rect.min_Rec[0] = queryBranch->m_rect.max_Rec[0] = qx;
    queryBranch->m_rect.min_Rec[1] = queryBranch->m_rect.max_Rec[1] = qy;
    queryBranch->CalcuKeyWordWeight(qText);
    queryBranch->level = -1;

    // 2. 使用 priority_queue 替代 vector+sort
    priority_queue<SearchItem> pq;

    // 3. 初始层入队
    for (auto & child : root->mBranch) {
        // root node record its child branch with id and level place
        int level_i = child->level;
        int id = child->id;

        // access i-th level with real address and other levels with random addressess
        if(id == -201) {
            std::cout<<std::endl;
        }
        auto child_branch = Access(id, level_i);
        child_branch->trueData = client_->cryptor_->aes_decrypt(child_branch->trueData, level_i); // decrypt the data using secret key in level i
        child_branch->level = -1; // this branch will be placed to client memory(marked level -1)

        double score = root->CalcuTestSPaceRele(child_branch, queryBranch); 
        pq.push({score, child});

        // if the client memory is full, it will be removed to the first empty level in HOTree
        if(client_->stash_.size() % Z == 0 && client_->stash_.size() != 0) {
            Eviction(client_); 
        }
    }
 
    // 4. 循环搜索
    while (!pq.empty() && results.size() < k) {
        SearchItem top = pq.top();
        pq.pop(); // O(log N) 操作，比 erase 高效得多

        Branch* curr = top.branch;

        // 【关键逻辑】检查是否已经到达叶子节点
        if (curr->id >= 0) { // id < 0 represents non-leaf branch
            if (id_to_record_map.count(curr->id)) {
                results.push_back(make_pair(top.score, id_to_record_map.at(curr->id)));
            }
        } else {
            // 是中间节点：展开子节点
            Node* internalNode = curr->childNode;
            if (internalNode) {
                for (auto* child : internalNode->mBranch) {
                    if (child->is_empty_data) continue;
                    
                    int level_i = child->level;
                    int id = child->id;

                    // access i-th level with real address and other levels with random addressess
                    auto child_branch = Access(id, level_i);
                    child_branch->trueData = client_->cryptor_->aes_decrypt(child_branch->trueData, level_i); // decrypt the data using secret key in level i
                    child_branch->level = -1; // this branch will be placed to client memory(marked level -1)
                    
                    
                    // 必须确保 CalcuTestSPaceRele 对中间节点返回的是“乐观估计值”
                    // 即：MinDist(空间) 和 MaxWeight(文本) 组合出的最高分
                    double score = internalNode->CalcuTestSPaceRele(child, queryBranch);
                    pq.push({score, child});

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
    id_to_record_map.clear();
    vector<Branch*> position_branchs;
    int temp_ID = 0;

    // 1. 数据转换为 Branch
    for (const auto& data : raw_data) {
        id_to_record_map[data.id] = data;

        Branch* mBranch = new Branch();
        mBranch->is_empty_data = false;
        mBranch->id = data.id;
        mBranch->textID = temp_ID;
        mBranch->text = data.processed_text;
        // mBranch->trueData = client_->cryptor_->aes_encrypt(padZero(data.processed_text), L); // ciphertext, it has been padded to blocksize before encrypting
        
        // 使用 Branch 的计算方法，它会使用全局 dic_str
        // 这里模拟 "CalcuKeyWordWeight" (TF) 或 "CalcuKeyWordRele" (Similarity)
        // 按照 Branch.h 的逻辑，CalcuKeyWordRele 是计算相似度，这里我们使用 TF 逻辑更符合一般检索
        // 但为了兼容结构体方法，我们调用:
        mBranch->CalcuKeyWordWeight(mBranch->text); //当前使用的是叶子节点，因此，他需要跟每个关键词计算相似度
        // mBranch->CalcuKeyWordRele(mBranch->text);

        mBranch->m_rect.min_Rec[0] = mBranch->m_rect.max_Rec[0] = data.x_coord;
        mBranch->m_rect.min_Rec[1] = mBranch->m_rect.max_Rec[1] = data.y_coord;
        // mBranch->level = L; // note that it is the last level initally
        mBranch->pointBranch = mBranch;
        mBranch->curNode = nullptr;

        // append the leaf branch to vector
        all_branchs.push_back(mBranch);


        position_branchs.push_back(mBranch);
        temp_ID++;
    }
    int data_num = position_branchs.size();

    // 3. STR 构建
    vector<Node*> nodes;
    
    // X 轴排序
    sort(position_branchs.begin(), position_branchs.begin() + data_num, 
         [](Branch* a, Branch* b) { return a->m_rect.min_Rec[0] < b->m_rect.min_Rec[0]; });

    int current_idx = 0;
    while (current_idx < position_branchs.size()) {//每次处理MAX_SIZE个数据
        int end_idx = min((size_t)(current_idx + MAX_SIZE), position_branchs.size());
        
        // Y 轴局部排序
        if (current_idx < data_num) {
             int sort_end = min(end_idx, data_num);
             sort(position_branchs.begin() + current_idx, position_branchs.begin() + sort_end,
                  [](Branch* a, Branch* b) { return a->m_rect.min_Rec[1] < b->m_rect.min_Rec[1]; });
        }

        Node* m_node = new Node(0);
        m_node->initRectangle(); // 初始化矩形
        //MAX_SIZE个数据放在一个节点m_node中，这个node包含这些branch的矩形
        for (int i = current_idx; i < end_idx; i++) {
            Branch* b = position_branchs[i];
            b->curNode = m_node;
            m_node->mBranch.push_back(b);
            // ✅ 使用 Node.h 定义的 rectUpdate
            m_node->rectUpdate(b); 
        }
        
        m_node->setCount(); //记录有多少个真实节点
        m_node->id = current_idx / MAX_SIZE;
        nodes.push_back(m_node);
        current_idx = end_idx;
    }

    // 向上构建，构建父节点
    int non_leaf_branch_id = -1;
    while (nodes.size() > 1) {
        vector<Node*> parent_nodes; //记录当前层的节点向量
        int node_idx = 0;
        
        while (node_idx < nodes.size()) {
            Node* parent = new Node(); //一个节点存MAX_SIZE个branch
            parent->initRectangle();

            int pack_count = 0;
            while (pack_count < MAX_SIZE && node_idx < nodes.size()) { //遍历孩子节点，每MAX_SIZE变成一个新的branch
                Node* childNode = nodes[node_idx];

                Branch* pBranch = new Branch();
                pBranch->m_rect = childNode->m_rect; // 复制子节点 MBR
                // pBranch->level = L;
                pBranch->childNode = childNode;
                childNode->parentNode = parent;
                pBranch->id = non_leaf_branch_id--;

                // 聚合权重
                pBranch->weight.resize(dic_str.size(), 0.0);
                for(auto* b : childNode->mBranch) {
                     if(!b->is_empty_data) pBranch->keyWeightUpdate(b);
                }

                pBranch->curNode = parent;
                parent->mBranch.push_back(pBranch);
                parent->rectUpdate(pBranch); // 更新父节点 MBR
                
                // append the non-leaf branch to vector
                all_branchs.push_back(pBranch);
                
                node_idx++;
                pack_count++;
            }
            parent->setCount();
            parent_nodes.push_back(parent);
        }
        nodes = parent_nodes;
    }
    if (!nodes.empty()) root = nodes[0];

    int N = all_branchs.size();
    int l = ceil(log2(Z));   //the lowest level
    int L = ceil(log2(N)); //the top level
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
        branch->level = L;
        branch->trueData = client_->cryptor_->aes_encrypt(padZero(branch->text), L); // ciphertext, it has been padded to blocksize before encrypting
        vec_hashtable_[L]->insert(*branch, client_);
    }
    for(int i = 0; i < vec_hashtable_[L]->stash.size(); i++) {
        // move the stash to client, stash is so small that client is easy to save
        client_->vector_every_level_stash_[L].push_back(vec_hashtable_[L]->stash[i]);
    }
    client_->vec_hotree_level_i_is_empty_[L] = false; 
}