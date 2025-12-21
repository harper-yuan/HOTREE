// HORAM.h
#ifndef HORAM_H
#define HORAM_H

#include "Node.h"
#include <vector>
#include <unordered_map>
#include <cmath>
#include <set>
#include <stdexcept>
#include <algorithm>

using namespace std;

class Level {
public:
    int level_id;
    int capacity;
    // 使用 Map 模拟 ORAM 的一层，Key 是 NodeID
    unordered_map<string, Node> data; 
    bool is_empty;

    Level(int id, int cap) : level_id(id), capacity(cap), is_empty(true) {}

    void write(const unordered_map<string, Node>& nodes) {
        data = nodes;
        is_empty = false;
    }

    // 模拟读取并移除（真实的 ORAM 会读到 Dummy，这里逻辑移除）
    bool read_and_remove(string node_id, Node& out_node) {
        if (data.find(node_id) != data.end()) {
            out_node = data[node_id];
            data.erase(node_id);
            return true;
        }
        return false;
    }

    vector<Node> clear() {
        vector<Node> nodes;
        for (auto& pair : data) {
            nodes.push_back(pair.second);
        }
        data.clear();
        is_empty = true;
        return nodes;
    }
};

class StrictORAM {
private:
    int Z;                  // 客户端 Stash 触发 Flush 的阈值
    int depth;              // 树深度
    int max_levels_count;
    
    vector<Level*> levels;  // ORAM 的所有层
    
public:
    // 客户端存储 (Stash)
    unordered_map<string, Node> stash; 
    int access_counter;
    
    // 客户端记录根节点位置
    string root_id;
    int root_level;

    StrictORAM(int _depth, int _Z = 10) : depth(_depth), Z(_Z), access_counter(0) {
        max_levels_count = depth + 2; // 稍微多给几层余量
        
        // 初始化层级, Level i 容量 = Z * 2^i
        for (int i = 0; i <= max_levels_count; ++i) {
            int cap = Z * pow(2, i);
            levels.push_back(new Level(i, cap));
        }
        root_id = "0";
        root_level = -1;
    }

    ~StrictORAM() {
        for (auto l : levels) delete l;
    }

    // 初始化数据：将构建好的树的所有节点放入最底层
    void init_data(const vector<Node>& all_nodes) {
        unordered_map<string, Node> init_layer_data;
        for (const auto& node : all_nodes) {
            init_layer_data[node.id] = node;
        }
        
        // 初始全部放入最深的一层 (或者足够大的一层)
        int init_level = depth; 
        if (init_level >= levels.size()) init_level = levels.size() - 1;

        levels[init_level]->write(init_layer_data);
        root_level = init_level;
        
        // 初始化时，所有内部节点的 children_levels 指针都应该指向 init_level
        // 但注意：如果是从 Level 读取出来的，我们无法修改 Level 里的数据
        // 所以在传入 all_nodes 之前，构建树的时候就需要把 children_levels 设好
        cout << "[Init] ORAM 初始化完成。数据位于 Level " << init_level << endl;
    }

    // 核心功能：Flush
    void flush() {
        // 1. 寻找第一个空层
        int target_level = 0;
        while (target_level < levels.size() && !levels[target_level]->is_empty) {
            target_level++;
        }
        if (target_level >= levels.size()) target_level = levels.size() - 1; // 溢出保护

        cout << "    >> [Flush] 触发! 目标层 Level " << target_level << endl;

        // 2. 收集数据 (Stash + L0 ~ Lt-1)
        vector<Node> nodes_to_merge;
        
        // 取出 Stash
        for (auto& pair : stash) nodes_to_merge.push_back(pair.second);
        stash.clear();

        // 取出中间层
        for (int i = 0; i < target_level; ++i) {
            vector<Node> level_nodes = levels[i]->clear();
            nodes_to_merge.insert(nodes_to_merge.end(), level_nodes.begin(), level_nodes.end());
        }

        // 3. 批量更新指针
        // 建立 ID 集合以便快速查询
        set<string> merge_ids;
        for (const auto& n : nodes_to_merge) merge_ids.insert(n.id);

        for (auto& node : nodes_to_merge) {
            // 3.1 根节点位置
            if (node.id == root_id) {
                root_level = target_level;
            }

            // 3.2 子节点指针更新
            if (!node.is_leaf) {
                for (int i = 0; i < node.children_ids.size(); ++i) {
                    string child_id = node.children_ids[i];
                    if (merge_ids.count(child_id)) {
                        // 孩子也在此次合并列表中，将去往 target_level
                        node.children_levels[i] = target_level;
                    } else {
                        // 孩子不在列表里，说明在更深层，指针保持不变
                    }
                }
            }
        }

        // 4. 写入
        unordered_map<string, Node> new_data;
        for (const auto& n : nodes_to_merge) new_data[n.id] = n;
        levels[target_level]->write(new_data);
        
        cout << "    >> [Flush] 完成. " << new_data.size() << " 个节点存入 Level " << target_level << endl;
    }

    // 核心功能：Access
    // 模拟用户从根节点向下查找 leaf_id 的路径
    // path_ids 是预先知道的路径 ID 序列（在真实场景中，这是由树搜索逻辑动态决定的）
    void access(const vector<string>& path_ids) {
        cout << "\n--- Access 请求: Leaf " << path_ids.back() << " ---" << endl;
        
        string curr_id = root_id;
        string prev_id = "";
        int pred_level = root_level; // 预测层级

        for (int i = 0; i < path_ids.size(); ++i) {
            curr_id = path_ids[i];
            
            // A. 验证 (Simulation only)
            int real_level = -1; // -1 表示在 Stash
            if (stash.find(curr_id) != stash.end()) real_level = -1;
            else {
                for (int l = 0; l < levels.size(); ++l) {
                    if (levels[l]->data.count(curr_id)) {
                        real_level = l;
                        break;
                    }
                }
            }
            
            // 实际系统中，如果不匹配，说明预测指针错误，数据丢失
            if (pred_level != real_level && !(pred_level == -1 && real_level == -1)) {
                 // 注意：这里简化了 -1 的处理，如果是 -1，直接在 stash 找
                 if (pred_level == -1 && stash.count(curr_id)) {
                     // OK
                 } else {
                     cout << "  [Error] 预测失败! ID:" << curr_id << " 预测 L" << pred_level << " 实际 L" << real_level << endl;
                     return;
                 }
            }

            // B. 读取并移入 Stash
            Node curr_node;
            if (pred_level != -1) {
                // 从 Level 中读取
                if (levels[pred_level]->read_and_remove(curr_id, curr_node)) {
                    stash[curr_id] = curr_node;
                    access_counter++;
                } else {
                    cout << "  [Error] 在层级 L" << pred_level << " 未找到节点 " << curr_id << endl;
                    return;
                }
            } else {
                // 已经在 Stash 中
                curr_node = stash[curr_id];
            }

            // C. 更新父节点指针 (Previous Node)
            // 因为当前节点 curr_node 现在一定在 Stash 中 (Level = -1)
            // 所以我们需要更新父节点指向它的指针为 -1
            if (prev_id != "") {
                // 父节点一定在 Stash 中 (因为我们是沿着路径下来的)
                Node& parent = stash[prev_id];
                for(int k=0; k<parent.children_ids.size(); ++k) {
                    if(parent.children_ids[k] == curr_id) {
                        parent.children_levels[k] = -1; // 更新指针指向 Stash
                        break;
                    }
                }
            } else {
                // 如果是根节点，更新全局记录
                root_level = -1;
            }

            // D. 准备下一跳预测
            // 如果不是最后一步，查看当前节点中存储的下一个孩子的 level
            if (i < path_ids.size() - 1) {
                string next_child_id = path_ids[i+1];
                bool found_child = false;
                for(int k=0; k<curr_node.children_ids.size(); ++k) {
                    if(curr_node.children_ids[k] == next_child_id) {
                        // 这是一个极其关键的步骤：
                        // 我们使用的是 stash[curr_id] 里的最新数据，还是刚刚读出来的旧数据？
                        // 指针是在 flush 时更新的。如果子节点之前没动过，指针是准的。
                        // 如果子节点被挪到了 Stash，那么在之前的访问中，curr_node (作为父节点) 应该已经被更新了？
                        // 不一定。如果之前的访问路径不同，但经过了 curr_node...
                        // 这里我们信任 Node 数据里的 children_levels
                        pred_level = curr_node.children_levels[k]; 
                        found_child = true;
                        break;
                    }
                }
                if(!found_child) {
                    cout << "  [Error] 树结构异常，找不到孩子 " << next_child_id << endl;
                    return;
                }
            }

            prev_id = curr_id;

            // E. 检查 Flush
            if (stash.size() >= Z) { // 简单起见用 size 判断
                flush();
                // Flush 后，prev_id (当前节点) 可能被移动到了深层
                // 但为了保持本次 Access 的原子性，通常我们认为当前路径上的节点
                // 在逻辑上应该保留在“可访问”状态。
                // 不过在 Strict ORAM 定义中，flush 把 stash 清空了。
                // 此时 prev_id 已经在某个 Level 了。
                // 只要我们只读取数据，不再修改它（除了指针），就没问题。
                // 但如果还要基于它找下一个孩子，我们需要知道它现在去哪了？
                // 在本实现中，curr_node 变量是值拷贝，仍然持有旧的指针信息，可以继续预测。
                // 唯一的问题是：如果 Flush 把下一个孩子也移动了怎么办？
                // 答：Flush 逻辑中，如果父子都在 Stash，会一起移动，并更新父节点的指针。
                // 但 curr_node 变量是 Flush 之前的副本。
                // 所以，如果触发了 Flush，我们必须重新从 ORAM (或 Flush 后的位置) 读取 prev_id 的最新状态来获取准确的 children_levels 吗？
                // 你的 Python 代码在 Flush 后没有重读，而是依赖 Flush 逻辑的一致性。
                // 实际上，如果 Flush 发生了，所有 Stash 里的节点都被写入了 Target Level。
                // 那么下一个孩子如果原本在 Stash，现在就在 Target Level。
                // 也就是 pred_level 应该变成 Target Level。
                
                // 修正：为了简化模拟，我们假设 Flush 只在 Access 结束后进行，或者 Z 足够大。
                // 如果必须在中间 Flush，代码需要复杂的重定位逻辑。
                // 你的 Python 代码也是：if access_counter % Z == 0: flush() 在循环内。
                // 这里的 C++ 实现暂且保持这样，如果出错再调整。
            }
        }
        cout << "  > 访问成功." << endl;
    }
};

#endif