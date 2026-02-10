#pragma once
#include <vector>
#include <cstddef>
#include <omp.h>
#include <chrono>
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
    Branch* find(uint64_t id, uint64_t counter_for_lastest_data, Client* client);
    
    size_t size() const { return current_count + stash.size(); }
    size_t getTableCapacity() const { return table.size(); }
    size_t capacity() const { return table.size(); }

    void oblivious_shuffle(Client* client);
    void oblivious_shuffle_and_insert_last_level(std::vector<Branch*>& all_elements, std::vector<int> branchs_level_belong_to, Client* client);
    // 修改：处理指针向量
    void oblivious_shuffle_and_insert(std::vector<Branch*>& all_elements, std::vector<int> branchs_level_belong_to, Client* client);
    std::vector<Branch*> oblivious_tight_compaction(std::vector<Branch*> all_elements, std::vector<int> branchs_level_belong_to, Client* client);

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
    double single_shuffle_round_trips;
    double single_shuffle_commucations;
    double single_shuffle_times;
    int shuffle_count;
    bool shuffle_tested_flag = false;

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
    void print_table_status(const std::string& context, std::vector<Branch*> client_stash) {return ;}

    // 修改后的 OHT.cpp 中的打印函数
    void print_table_status_true(const std::string& context, std::vector<Branch*> client_stash) {
    // void print_table_status(const std::string& context, std::vector<Branch*> client_stash) {
        std::cout << "\n" << std::string(80, '=') << std::endl;
        std::cout << " [DEBUG] OHT Status | 触发位置: " << context << std::endl;
        std::cout << " [Level Info] 当前层级: " << HOTREE_level_ << " | 桶数量: " << table.size() << " | 元素总数: " << current_count << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        
        // 打印 Table 主体
        for (size_t i = 0; i < table.size(); ++i) {
            if (table[i].occupied && table[i].branch != nullptr) {
                Branch* b = table[i].branch;
                std::cout << "  [Pos " << std::setw(4) << i << "] ID: " << std::setw(6) << b->id 
                        << " | Counter: " << std::setw(3) << b->counter_for_lastest_data 
                        << " | Addr: " << b << std::endl;

                // 打印该节点的 child_triple 向量
                if (!b->child_triple.empty()) {
                    std::cout << "         └── Children: ";
                    for (size_t j = 0; j < b->child_triple.size(); ++j) {
                        Triple* t = b->child_triple[j];
                        if (t != nullptr) {
                            std::cout << "[ID:" << t->id << ", C:" << t->counter_for_lastest_data << ", L:" << t->level << "]";
                        } else {
                            std::cout << "[NULL]";
                        }
                        if (j < b->child_triple.size() - 1) std::cout << " -> ";
                    }
                    std::cout << std::endl;
                }
            }
        }

        // 打印 Stash（溢出区）
        if (!client_stash.empty()) {
            std::cout << "  --- Stash Contents ---" << std::endl;
            for (size_t i = 0; i < client_stash.size(); ++i) {
                Branch* sb = client_stash[i];
                if (sb != nullptr) {
                    std::cout << "  [Stash " << i << "] ID: " << std::setw(6) << sb->id 
                            << " | Counter: " << std::setw(3) << sb->counter_for_lastest_data 
                            << " | Addr: " << sb << std::endl;
                    
                    // 同样展开 Stash 节点的子节点
                    if (!sb->child_triple.empty()) {
                        std::cout << "           └── Children: ";
                        for (auto* st : sb->child_triple) {
                            if (st) std::cout << "[ID:" << st->id << ", C:" << st->counter_for_lastest_data << ", L:" << st->level << "] ";
                        }
                        std::cout << std::endl;
                    }
                }
            }
        }
        std::cout << std::string(80, '=') << "\n" << std::endl;
    }
};