#pragma once
#include <vector>
#include <map>
#include <algorithm>
#include <functional>
#include "define.h"
#include "DataReader.h"
#include "cryptor.h"
#include "OHT.h" // cuckoo table
#include "Branch.h"
#include "client.h"

class HOTree {
public:
    std::vector<std::string> dic_str;
    std::map<std::string, int> dic_map;
    std::vector<Triple*> root;
    std::vector<DataRecord> id_to_record_vec; 
    Client* client_;
    std::vector<std::unique_ptr<CuckooTable>> vec_hashtable_;
    std::vector<Branch*> all_branchs; // for secure free memory
    
public:
    HOTree(const std::vector<std::string>& dict);
    ~HOTree();
    
    void Build(std::vector<DataRecord>& raw_data, Client*& client);
    void Eviction(Client* client);
    Client* getClient();
    std::vector<std::pair<double, DataRecord>> SearchTopK(double qx, double qy, std::string qText, int k, Client* client);
    Branch* Retrieve(Client* client_, Triple*& triple);
    Branch* Access(uint64_t id, int counter_for_lastest_data, int level_i);
    Branch* Self_healing_Access(int id, int counter_for_lastest_data, int prediction_level);
    void PerformGarbageCollection(int target_level);

    // for debug
    std::vector<double> GetTextWeight(std::string text);
    void print_stash();
    void findid(int target_id);

    // for evaluation
    double compute_additional_oblivious_shuffle_time();
    void clear_additional_oblivious_shuffle_time();
};

