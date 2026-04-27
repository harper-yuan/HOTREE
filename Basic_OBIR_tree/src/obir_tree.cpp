#include "obir_tree.h"
#include <iostream>
#include <random>
#include <queue>

using namespace std;

OBIRTree::OBIRTree(const vector<string>& dict) {
    dic_str = dict;
    for (size_t i = 0; i < dict.size(); ++i) {
        dic_map[dict[i]] = (int)i;
    }
    dummy_block = new Branch(true, false);
    all_branchs.push_back(dummy_block);
}

OBIRTree::~OBIRTree() {
    for (auto* b : all_branchs) {
        if (b) delete b;
    }
    all_branchs.clear();
    for (auto* t : root_triples) {
        if (t) delete t;
    }
    root_triples.clear();
}

void OBIRTree::Build(vector<DataRecord>& raw_data, Client*& client) {
    if (raw_data.empty()) return;
    
    id_to_record_vec = std::move(raw_data);
    vector<Branch*> position_branchs;

    // 1. Convert DataRecord to Branch (Leaf Nodes)
    for (const auto& data : id_to_record_vec) {
        Branch* mBranch = new Branch();
        mBranch->is_empty_data = false;
        mBranch->id = data.id;
        mBranch->text = data.processed_text;
        mBranch->CalcuKeyWordWeight(mBranch->text, dic_str);
        mBranch->m_rect.min_Rec[0] = mBranch->m_rect.max_Rec[0] = data.x_coord;
        mBranch->m_rect.min_Rec[1] = mBranch->m_rect.max_Rec[1] = data.y_coord;
        all_branchs.push_back(mBranch);
        position_branchs.push_back(mBranch);
    }

    // 2. Build IR-tree using STR logic
    vector<Branch*> Branchs_at_IR_tree;
    sort(position_branchs.begin(), position_branchs.end(), 
         [](Branch* a, Branch* b) { return a->m_rect.min_Rec[0] < b->m_rect.min_Rec[0]; });

    int current_idx = 0;
    int non_leaf_branch_id = -1;
    while (current_idx < position_branchs.size()) {
        int end_idx = min((int)(current_idx + MAX_SIZE), (int)position_branchs.size());
        sort(position_branchs.begin() + current_idx, position_branchs.begin() + end_idx,
             [](Branch* a, Branch* b) { return a->m_rect.min_Rec[1] < b->m_rect.min_Rec[1]; });

        Branch* parent_branch = new Branch();
        parent_branch->id = non_leaf_branch_id--;
        parent_branch->initRectangle();
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

    while (Branchs_at_IR_tree.size() > 1) {
        vector<Branch*> next_level;
        int idx = 0;
        while (idx < Branchs_at_IR_tree.size()) {
            Branch* parent = new Branch();
            parent->id = non_leaf_branch_id--;
            parent->initRectangle();
            int count = 0;
            while (count < MAX_SIZE && idx < Branchs_at_IR_tree.size()) {
                Branch* child = Branchs_at_IR_tree[idx++];
                child->weight.resize(dic_str.size(), 0.0);
                parent->keyWeightUpdate(child);
                parent->rectUpdate(child);
                Triple* temp_triple = new Triple(child->id, 0, 0);
                parent->child_triple.push_back(temp_triple);
                parent->child_branch.push_back(child);
                parent->child_rects.push_back(child->m_rect);
                parent->child_weights_vec.push_back(child->weight);
                count++;
            }
            next_level.push_back(parent);
            all_branchs.push_back(parent);
        }
        Branchs_at_IR_tree = next_level;
    }

    if (!Branchs_at_IR_tree.empty()) {
        Branch* root = Branchs_at_IR_tree[0];
        root_triples.push_back(new Triple(root->id, 0, 0));
    }

    // 3. Setup Path-ORAM
    int N = all_branchs.size();
    H = ceil(log2(N)) + 1; // Tree height
    num_leaves = 1 << H;
    int num_nodes = (1 << (H + 1)) - 1;
    server_tree.resize(num_nodes);
    
    // Initialize buckets with dummy blocks
    for(int i = 0; i < num_nodes; i++) {
        server_tree[i].blocks.resize(Z_oram, nullptr);
    }

    if (client == nullptr) {
        client = new Client(H);
    }
    client_ = client;

    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(0, num_leaves - 1);

    unordered_map<int, int> node_to_leaf;
    for (auto* b : all_branchs) {
        if(b->is_empty_data && b == dummy_block) continue;
        int leaf = dis(gen);
        b->level = leaf; 
        node_to_leaf[b->id] = leaf;
    }

    for (auto* b : all_branchs) {
        if(b->is_empty_data && b == dummy_block) continue;
        for (auto* t : b->child_triple) {
            t->level = node_to_leaf[t->id];
        }
    }
    for (auto* t : root_triples) {
        t->level = node_to_leaf[t->id];
    }

    // Initial encryption and placement
    for (auto* b : all_branchs) {
        if(b->is_empty_data && b == dummy_block) continue;
        int leaf = b->level;
        bool placed = false;
        for (int l = H; l >= 0; l--) {
            int node_idx = GetNodeIndexOnPath(leaf, l);
            auto& bucket = server_tree[node_idx];
            for (int j = 0; j < Z_oram; j++) {
                if (bucket.blocks[j] == nullptr) {
                    b->trueData = client_->cryptor_->aes_encrypt(padZero(b->text), l);
                    bucket.blocks[j] = b;
                    placed = true;
                    break;
                }
            }
            if (placed) break;
        }
        if (!placed) {
            client_stash[b->id] = b;
        }
    }
}

int OBIRTree::GetNodeIndexOnPath(int leaf, int level) {
    int node_idx = 0;
    for (int i = 0; i < level; i++) {
        int bit = (leaf >> (H - 1 - i)) & 1;
        node_idx = 2 * node_idx + 1 + bit;
    }
    return node_idx;
}

void OBIRTree::PathRead(int leaf_index) {
    client_->communication_round_trip_ += 0.5;
    for (int l = 0; l <= H; l++) {
        int node_idx = GetNodeIndexOnPath(leaf_index, l);
        client_->communication_volume_ += BlockSize * Z_oram;
        auto& bucket = server_tree[node_idx];
        
        // Decrypt all blocks in the bucket
        for (int j = 0; j < Z_oram; j++) {
            Branch* b = bucket.blocks[j];
            if (b != nullptr) {
                // Perform decryption overhead
                b->trueData = client_->cryptor_->aes_decrypt(b->trueData, l);
                client_stash[b->id] = b;
                bucket.blocks[j] = nullptr;
            } else {
                // Even if the block is null (empty), simulate the decryption overhead
                // using a reusable dummy block's data.
                dummy_block->trueData = client_->cryptor_->aes_decrypt(padZero(""), l);
            }
        }
    }
}

void OBIRTree::PathWrite(int leaf_index) {
    client_->communication_round_trip_ += 0.5;
    for (int l = H; l >= 0; l--) {
        int node_idx = GetNodeIndexOnPath(leaf_index, l);
        auto& bucket = server_tree[node_idx];
        client_->communication_volume_ += BlockSize * Z_oram;
        
        int bucket_occupied = 0;
        // Try to push real blocks into the bucket
        for (auto it = client_stash.begin(); it != client_stash.end(); ) {
            Branch* b = it->second;
            if (bucket_occupied < Z_oram && IsOnPath(b->level, node_idx, l)) {
                // Re-encrypt the block for this level
                b->trueData = client_->cryptor_->aes_encrypt(padZero(b->text), l);
                bucket.blocks[bucket_occupied++] = b;
                it = client_stash.erase(it);
            } else {
                ++it;
            }
        }
        
        // Fill remaining slots with dummy encryption to simulate full bucket overhead
        while (bucket_occupied < Z_oram) {
            dummy_block->trueData = client_->cryptor_->aes_encrypt(padZero(""), l);
            bucket.blocks[bucket_occupied++] = nullptr;
        }
    }
}

bool OBIRTree::IsOnPath(int leaf, int node_index, int level) {
    return GetNodeIndexOnPath(leaf, level) == node_index;
}

Branch* OBIRTree::Retrieve(Triple*& triple) {
    int id = triple->id;
    int leaf = triple->level;

    if (client_stash.count(id)) {
        // Still need to perform dummy path access for obliviousness
        // always write back to same path. We only want correctness and it does not affect efficiency.
        PathRead(leaf);
        PathWrite(leaf);
        return client_stash[id];
    }

    PathRead(leaf);
    Branch* res = nullptr;
    if (client_stash.count(id)) {
        res = client_stash[id];
    }
    
    PathWrite(leaf);
    return res;
}

struct LazySearchItem {
    double score;
    Triple* triple_info;
    Branch* fetched_branch;
    bool operator<(const LazySearchItem& other) const {
        return score < other.score; 
    }
};

vector<pair<double, DataRecord>> OBIRTree::SearchTopK(double qx, double qy, string qText, int k, Client* client) {
    vector<pair<double, DataRecord>> results;
    if (root_triples.empty() || k <= 0) return results;

    Branch* queryBranch = new Branch();
    queryBranch->m_rect.min_Rec[0] = queryBranch->m_rect.max_Rec[0] = qx;
    queryBranch->m_rect.min_Rec[1] = queryBranch->m_rect.max_Rec[1] = qy;
    queryBranch->CalcuKeyWordWeight(qText, dic_str);

    priority_queue<LazySearchItem> pq;
    for (auto & triple : root_triples) {
        auto child_branch = Retrieve(triple);
        double score = client_->CalcuTestSPaceRele(child_branch, queryBranch);
        pq.push({score, triple, child_branch}); 
    }

    double min_score = -1.0;
    while (!pq.empty() && results.size() < k) {
        LazySearchItem top = pq.top();
        pq.pop();

        if (results.size() >= k && top.score < min_score) break;

        Branch* curr = top.fetched_branch;
        if (curr == nullptr) {
            curr = Retrieve(top.triple_info);
        }

        if (curr->child_triple.empty() || curr->id >= 0) {
            results.push_back(make_pair(top.score, id_to_record_vec[curr->id]));
            if (results.size() >= k) min_score = results.back().first;
        } else {
            size_t child_count = curr->child_triple.size();
            for (size_t i = 0; i < child_count; i++) {
                Triple* child_t = curr->child_triple[i];
                Branch* stub = new Branch();
                stub->m_rect = curr->child_rects[i];
                stub->weight = curr->child_weights_vec[i];
                double score = client_->CalcuTestSPaceRele(stub, queryBranch);
                delete stub;
                pq.push({score, child_t, nullptr});
            }
        }
    }
    delete queryBranch;
    return results;
}

void OBIRTree::print_stash() {
    cout << "Client Stash size: " << client_stash.size() << endl;
}
