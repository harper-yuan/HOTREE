// main.cpp
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <queue>
#include "Node.h"
#include "HORAM.h"

using namespace std;

// 简单的字符串分割
vector<string> split(const string& s, char delimiter) {
    vector<string> tokens;
    string token;
    istringstream tokenStream(s);
    while (getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

// 1. 加载数据集
vector<Node> load_dataset(const string& filename) {
    vector<Node> leaves;
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Error: Cannot open " << filename << endl;
        return leaves;
    }

    string line;
    int id_counter = 0;
    while (getline(file, line)) {
        if (line.empty()) continue;
        if (line.find(".id"))
                parent.children_ids.push_back(current_layer[i+1].id);
                
                // --- 关键：初始化 ORAM 指针 ---
                // 初始状态所有节点都在 init_level
                parent.children_levels[0] = init_level;
                parent.children_levels[1] = init_level;
                
                // 简单的 R-tree 逻辑：父节点包含子节点的数据摘要（这里略过，只做拓扑连接）
                next_layer.push_back(parent);
            } else {
                // 孤立节点，直接提升
                next_layer.push_back(current_layer[i]);
            }
        }
        all_nodes.insert(all_nodes.end(), next_layer.begin(), next_layer.end());
        current_layer = next_layer;
    }
    
    // 最后一个是 Root
    if (!current_layer.empty()) {
        current_layer[0].id = "0"; // 强制 Root ID 为 "0" 以匹配 ORAM 代码
        all_nodes.back().id = "0"; 
    }

    return all_nodes;
}

// 辅助：在 all_nodes 中查找父节点到叶子的路径 ID
bool find_path(const string& target, const string& current, const unordered_map<string, Node>& map, vector<string>& path) {
    path.push_back(current);
    if (current == target) return true;

    const Node& node = map.at(current);
    if (node.is_leaf) {
        if (current != target) {
            path.pop_back();
            return false;
        }
        return true;
    }

    for (const string& child : node.children_ids) {
        if (find_path(target, child, map, path)) return true;
    }

    path.pop_back();
    return false;
}

int main() {
    // 1. 准备数据
    vector<Node> leaves = load_dataset("dataset.txt");
    if (leaves.empty()) return -1;

    // 计算树深度
    int N = leaves.size();
    int depth = ceil(log2(N));
    int Z = 20; // Stash 容量

    cout << "Building Tree with depth " << depth << "..." << endl;
    
    // 2. 构建树并初始化指针
    // 注意：init_level 通常设为最底层
    int init_level = depth + 1; // 稍微深一点确保能装下
    vector<Node> all_nodes = build_tree(leaves, init_level);
    
    // 为了方便查找路径，建立一个内存中的 Map (模拟 Oracle，实际客户端不知道这个)
    unordered_map<string, Node> oracle_map;
    vector<string> leaf_ids;
    for (const auto& n : all_nodes) {
        oracle_map[n.id] = n;
        if (n.is_leaf) leaf_ids.push_back(n.id);
    }

    // 3. 初始化 ORAM
    StrictORAM oram(depth + 2, Z);
    oram.init_data(all_nodes);

    // 4. 执行测试
    cout << "\n=== Starting Simulation (100 Accesses) ===" << endl;
    for (int i = 0; i < 100; ++i) {
        // 随机选择一个叶子
        string target_leaf = leaf_ids[rand() % leaf_ids.size()];
        
        // 获取路径 (在实际 IR-tree 中，这是通过搜索算法如 Best-First Search 动态生成的)
        // 这里我们需要预先知道路径来验证 ORAM 的 Access 逻辑是否顺畅
        vector<string> path;
        if (find_path(target_leaf, "0", oracle_map, path)) {
            // 执行 ORAM 访问
            oram.access(path);
        } else {
            cout << "Error: Path not found for " << target_leaf << endl;
        }
    }

    cout << "\n=== Verification Finished ===" << endl;
    return 0;
}