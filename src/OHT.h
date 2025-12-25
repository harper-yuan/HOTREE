#pragma once
#include <vector>
#include <cstddef>
#include <omp.h>
#include "Branch.h"
#include "define.h"
#include "client.h"

class CuckooTable {
public:
    explicit CuckooTable(size_t initial_size, int HOTREE_level);
    ~CuckooTable() = default;

    // 修改：接收 Branch 指针，避免对象拷贝
    void insert(Branch* branch, Client* client);
    std::vector<Branch*> find_hotree(uint64_t id, size_t place1, size_t place2);
    Branch* find(uint64_t id, Client* client);
    Branch* find_remove(uint64_t id, Client* client);
    
    size_t size() const { return current_count + stash.size(); }
    size_t getTableCapacity() const { return table.size(); }
    size_t capacity() const { return table.size(); }

    void oblivious_shuffle(Client* client);
    // 修改：处理指针向量
    void oblivious_shuffle_and_insert(std::vector<Branch*>& all_elements, std::vector<int> branchs_level_belong_to, Client* client);

public:
    struct Entry {
        Branch* branch = nullptr; // 修改：存储指针，仅占 8 字节
        bool occupied = false;
    };

    std::vector<Entry> table;
    std::vector<Branch*> stash; // 修改：存储指针
    const size_t STASH_CAPACITY = cuckoo_stash_size;

    size_t current_count;
    int HOTREE_level_;

    static const int MAX_KICKS = 500;

    size_t hash(uint64_t id, size_t seed) const;
    void rehash(size_t new_size, Client* client);
    void insert_internal(Branch* branch, Client* client); // 修改：接收指针
    void print_all_id() {
        for(auto & elem : table) {
            if(elem.branch != nullptr) {
                std::cout<< elem.branch->id << " ";
            }
        }
        std::cout<<std::endl;
    };
};