#pragma once
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <cmath>
#include "define.h"
#include "DataReader.h"
#include "cryptor.h"
#include "Branch.h"
#include "client.h"

struct ORAMBucket {
    std::vector<Branch*> blocks;
    ORAMBucket() {
        // We will initialize with Z_oram dummy blocks or empty pointers
    }
};

class OBIRTree {
public:
    static const int Z_oram = 6; // Path-ORAM bucket size
    int H; // Height of the Path-ORAM tree
    int num_leaves;
    std::vector<ORAMBucket> server_tree;
    std::unordered_map<int, Branch*> client_stash;
    
    std::vector<std::string> dic_str;
    std::map<std::string, int> dic_map;
    std::vector<Triple*> root_triples;
    std::vector<DataRecord> id_to_record_vec; 
    Client* client_;
    std::vector<Branch*> all_branchs; // for secure free memory
    Branch* dummy_block; // used for crypto overhead simulation on empty slots

public:
    OBIRTree(const std::vector<std::string>& dict);
    ~OBIRTree();
    
    void Build(std::vector<DataRecord>& raw_data, Client*& client);
    std::vector<std::pair<double, DataRecord>> SearchTopK(double qx, double qy, std::string qText, int k, Client* client);
    Branch* Retrieve(Triple*& triple);

    // Path-ORAM specific helpers
    int GetLeaf(int id); 
    void PathRead(int leaf_index);
    void PathWrite(int leaf_index);
    bool IsOnPath(int leaf, int node_index, int level);
    int GetNodeIndexOnPath(int leaf, int level);

    // for debug
    void print_stash();
};
