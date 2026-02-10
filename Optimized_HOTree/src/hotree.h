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

    // 【新增】读写锁：允许多个线程同时持有读锁（并行查询），
    // 但只允许一个线程持有写锁（独占驱逐）
    // mutable 关键字允许它在 const 函数中被修改（虽然这里没用到 const 函数，但加上是个好习惯）
    mutable std::shared_mutex rw_mutex_;
    // 【新增】保护 all_branchs 的互斥锁
    std::mutex all_branchs_mtx_; 
    
public:
    HOTree(const std::vector<std::string>& dict);
    ~HOTree();
    
    void Build(std::vector<DataRecord>& raw_data, Client*& client);
    void Eviction(Client* client);
    Client* getClient();
    Branch* Retrieve(Client* client_, Triple*& triple, std::shared_lock<std::shared_mutex>& read_lock, int user_id);
    std::vector<std::pair<double, DataRecord>> SearchTopK(double qx, double qy, std::string qText, int k, Client* client, int user_id);

    // std::vector<std::pair<double, DataRecord>> SearchTopK(double qx, double qy, std::string qText, int k, Client* client);
    // Branch* Retrieve(Client* client_, Triple*& triple);
    Branch* Access(uint64_t id, int counter_for_lastest_data, int level_i, int user_id);
    Branch* Self_healing_Access(int id, int counter_for_lastest_data, int prediction_level, int user_id);
    void PerformGarbageCollection(int target_level);

    // for debug
    std::vector<double> GetTextWeight(std::string text);
    void print_stash();
    void findid(int target_id);

    // for evaluation
    double compute_additional_oblivious_shuffle_time();
    void clear_additional_oblivious_shuffle_time();
};

