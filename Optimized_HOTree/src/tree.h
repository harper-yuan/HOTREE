#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <limits>
#include <map>
#include <queue>
#include <iomanip>
#include <set>
#include "define.h"

// --- 全局常量 ---
const int B = 4096; // 模拟 Block 大小

// 声明全局字典，以便 PlainBranch::CalcuKeyWordRele 使用 (兼容 PlainBranch.h 写法)
extern std::vector<std::string> dic_str;
extern std::map<std::string, int> dic_map;

// --- 前向声明 ---
class Node;

struct Child_Triple {
    int id;
    
    int level;
    int counter_for_lastest_data;
    
    // 构造函数
    Child_Triple(int a, int b, int c) : id(a), level(b), counter_for_lastest_data(c) {}
    
    // 也可以重载输出运算符
    friend std::ostream& operator<<(std::ostream& os, const Child_Triple& t) {
        os << "(" << t.id << ", " << t.level << ", " << t.counter_for_lastest_data << ")";
        return os;
    }
};

// --- PlainBranch 结构 (参考 PlainBranch.h) ---
class PlainBranch {
public:
    Node* childNode;          // 保存孩子节点
    Node* curNode;            // 当前节点
    Rectangle m_rect;         // 保存矩形数据
    std::string text;         // 保存文本数据
    std::vector<double> weight; 
    std::string trueData;     // 真实数据
    bool is_empty_data;       // 记录是否为空数据
    bool is_dummy_for_shuffle;// just for shuffle

    int counter_for_lastest_data; //记录最新数据的计数器
    int id;
    int level;
    PlainBranch* pointBranch;
    std::vector<PlainBranch*> child;
    std::vector<Child_Triple> child_triple;
    PlainBranch* partent;
    int textID;

    PlainBranch();
    PlainBranch(bool dummy);
    PlainBranch(bool empty_data, bool dummy_for_shuffle);

    bool operator<(const PlainBranch& other) const;
    
    void textUpdate(PlainBranch* mbranch);
    void LowerText(std::string &text);
    int levenshteinDistance(const std::string& s1, const std::string& s2);
    double similarity(const std::string& s1, const std::string& s2);
    
    // 按照 PlainBranch.h 定义，依赖全局 dic_str
    void CalcuKeyWordRele(std::string& text);
    void CalcuKeyWordWeight(std::string &text);

    void keyWeightUpdate(PlainBranch *nBranch);
    void rectUpdate(Rectangle *nRect);
    
    bool operator==(const PlainBranch& other) const;
};

// --- PlainSearchItem (辅助结构) ---
struct PlainSearchItem {
    double score;
    PlainBranch* branch;
    bool operator<(const PlainSearchItem& other) const {
        return score < other.score; 
    }
};

// --- Node 结构 (参考 Node.h) ---
class Node {
public:
    int count; //记录有多少个真实节点
    // std::vector<InvertedFile> child_iFile; // 暂时省略倒排索引以简化
    std::vector<PlainBranch*> mBranch;
    Rectangle m_rect;
    int id;
    Node* parentNode;
    int level_in_plain_tree; // 0 means leaf, 1 means non-leaf

    Node(int lvl = -1);

    static int randNum(int n);
    bool IsInternalNode();
    bool IsLeaf();
    void SetLevel();
    void setCount();
    
    // 核心更新函数
    void rectUpdate(PlainBranch *m_branch);
    void initRectangle();
    
    // 虚拟节点相关 (明文版简化)
    void addVirtualBranch(int temp_ID, std::string Com); 

    // 计算相关
    virtual double CalcuTextRelevancy(std::vector<double> weight1, std::vector<double> weight2);
    virtual double CalcuSpaceIncrease(Rectangle pre, Rectangle newr);
    virtual double CalcuTestSPaceRele(PlainBranch *n1, PlainBranch *n2);
    virtual Rectangle CombineRect(Rectangle *rect1, Rectangle *rect2);
    virtual std::vector<double> CombineKeyWords(std::vector<double> weight1, std::vector<double> weight2);
    
    // 辅助
    virtual void Lower(std::string &text);
};

// --- PlainIRTree 类 ---
class PlainIRTree {
private:
    Node* root = nullptr;
    std::map<int, DataRecord> id_to_record_map; // ID 到原始数据的映射

    void deleteTree(Node* node);

public:
    PlainIRTree(const std::vector<std::string>& dict);
    ~PlainIRTree();

    void Build(std::vector<DataRecord>& raw_data);
    std::vector<std::pair<double, DataRecord>> SearchTopK(double qx, double qy, std::string qText, int k);
    
    // 辅助测试
    std::vector<double> GetTextWeight(std::string text); 
};



