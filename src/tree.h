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

// 声明全局字典，以便 Branch::CalcuKeyWordRele 使用 (兼容 Branch.h 写法)
extern std::vector<std::string> dic_str;
extern std::map<std::string, int> dic_map;

// --- 前向声明 ---
class Node;

// --- Rectangle 结构 (参考 Branch.h) ---
struct Rectangle {
    double min_Rec[2];
    double max_Rec[2];

    Rectangle();
    double Area();
    bool operator==(const Rectangle& other) const { return false; }
    double MinDist(const Rectangle& other) const;
};

// --- Branch 结构 (参考 Branch.h) ---
class Branch {
public:
    Node* childNode;          // 保存孩子节点
    Node* curNode;            // 当前节点
    Rectangle m_rect;         // 保存矩形数据
    std::string text;         // 保存文本数据
    std::vector<double> weight; 
    std::string trueData;     // 真实数据
    bool is_empty_data;       // 记录是否为空数据
    bool is_dummy_for_shuffle;// just for shuffle

    int id;
    int level;
    Branch* pointBranch;
    std::vector<Branch*> child;
    Branch* partent;
    int textID;

    Branch();
    Branch(bool dummy);
    Branch(bool empty_data, bool dummy_for_shuffle);

    bool operator<(const Branch& other) const;
    
    void textUpdate(Branch* mbranch);
    void LowerText(std::string &text);
    int levenshteinDistance(const std::string& s1, const std::string& s2);
    double similarity(const std::string& s1, const std::string& s2);
    
    // 按照 Branch.h 定义，依赖全局 dic_str
    void CalcuKeyWordRele(std::string& text);
    void CalcuKeyWordWeight(std::string &text);

    void keyWeightUpdate(Branch *nBranch);
    void rectUpdate(Rectangle *nRect);
    
    bool operator==(const Branch& other) const;
};

// --- SearchItem (辅助结构) ---
struct SearchItem {
    double score;
    Branch* branch;
    bool operator<(const SearchItem& other) const {
        return score < other.score; 
    }
};

// --- Node 结构 (参考 Node.h) ---
class Node {
public:
    int count; //记录有多少个真实节点
    // std::vector<InvertedFile> child_iFile; // 暂时省略倒排索引以简化
    std::vector<Branch*> mBranch;
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
    void rectUpdate(Branch *m_branch);
    void initRectangle();
    
    // 虚拟节点相关 (明文版简化)
    void addVirtualBranch(int temp_ID, std::string Com); 

    // 计算相关
    virtual double CalcuTextRelevancy(std::vector<double> weight1, std::vector<double> weight2);
    virtual double CalcuSpaceIncrease(Rectangle pre, Rectangle newr);
    virtual double CalcuTestSPaceRele(Branch *n1, Branch *n2);
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



