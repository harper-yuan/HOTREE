#pragma once
#include <vector>
#include <cstddef>
#include <omp.h>
#include "tree.h"
#include "define.h"
#include "client.h" // Include Client for interaction

class CuckooTable {
public:
    explicit CuckooTable(size_t initial_size, int HOTREE_level);
    ~CuckooTable() = default;

    void insert(Branch& branch, Client* client);
    std::vector<Branch*> find_hotree(uint64_t id, size_t place1, size_t place2);
    Branch* find(uint64_t id, Client* client);
    Branch* find_remove(uint64_t id, Client* client);
    size_t size() const { return current_count + stash.size(); }
    size_t getTableCapacity() const { return table.size(); }
    size_t capacity() const { return table.size(); }

    // --- New Shuffle Operation ---
    // Performs oblivious shuffle using the Client to hide access patterns
    void oblivious_shuffle(Client* client);
    void oblivious_shuffle_and_insert(std::vector<Branch> all_elements, Client* client);

public:
    struct Entry {
        Branch branch;
        bool occupied = false;
    };

    std::vector<Entry> table;
    std::vector<Branch> stash;
    const size_t STASH_CAPACITY = cuckoo_stash_size;

    size_t current_count;
    int HOTREE_level_;
    // size_t seed1;
    // size_t seed2;

    static const int MAX_KICKS = 500;

    size_t hash(uint64_t id, size_t seed) const;
    void rehash(size_t new_size, Client* client);
    void insert_internal(Branch branch, Client* client);
};