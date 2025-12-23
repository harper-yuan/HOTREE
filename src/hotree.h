#include "define.h"
#include "tree.h"
#include "DataReader.h"
#include "cryptor.h"
#include "client.h"
#include "OHT.h" // cuckoo table

class HOTree {
private:
    Node* root = nullptr;
    std::vector<DataRecord> id_to_record_vec; 
    Client* client_;
    std::vector<std::unique_ptr<CuckooTable>> vec_hashtable_;
    
public:
    HOTree(const std::vector<std::string>& dict);
    ~HOTree();
    void DeleteNodeRecursive(Node* node);

    void Build(std::vector<DataRecord>& raw_data, Client*& client);
    void Eviction(Client* client);
    Client* getClient();
    std::vector<std::pair<double, DataRecord>> SearchTopK(double qx, double qy, std::string qText, int k, Client* client);
    Branch* Access(int id, int level_i);
    // 辅助测试
    std::vector<double> GetTextWeight(std::string text);
};