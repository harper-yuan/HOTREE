#pragma once
#include "define.h"

struct Triple {
    int id;
    int level;
    int counter_for_lastest_data;
    
    // 构造函数
    Triple(int a, int b, int c) : id(a), level(b), counter_for_lastest_data(c) {}
    
    Triple(const Triple* other) {
        if (other) {
            this->id = other->id;
            this->level = other->level;
            this->counter_for_lastest_data = other->counter_for_lastest_data;
        }
    }

    // 也可以重载输出运算符
    friend std::ostream& operator<<(std::ostream& os, const Triple& t) {
        os << "(" << t.id << ", " << t.level << ", " << t.counter_for_lastest_data << ")";
        return os;
    }
};



// --- Branch 结构 (参考 Branch.h) ---
class Branch {
public:
    int counter_for_lastest_data; //记录最新数据的计数器
    int id;
    int level;

    Rectangle m_rect;         // 保存矩形数据
    std::string text;         // 保存文本数据
    std::vector<double> weight; 
    std::string trueData;     // 真实数据
    bool is_empty_data;       // 记录是否为空数据
    bool is_dummy_for_shuffle;// just for shuffle
    std::vector<Rectangle> child_rects; 
    // 存储子节点的关键词权重，用于父节点直接计算子节点的文本相关性
    std::vector<std::vector<double>> child_weights_vec; 

    std::vector<Triple*> child_triple;
    std::vector<Branch*> child_branch;
    Branch* partent;

    Branch();
    Branch(bool dummy);
    Branch(bool empty_data, bool dummy_for_shuffle);
    Branch(const Branch* other);

    ~Branch();
    bool operator<(const Branch& other) const;
    
    void textUpdate(Branch* mbranch);
    void LowerText(std::string &text);
    int levenshteinDistance(const std::string& s1, const std::string& s2);
    double similarity(const std::string& s1, const std::string& s2);
    
    /* For similarity computation */
    void initRectangle() {
        for(int index = 0; index < 2; index++) {
            this->m_rect.min_Rec[index] = std::numeric_limits<double>::max();
            this->m_rect.max_Rec[index] = std::numeric_limits<double>::lowest();
        }
    }
    void rectUpdate(Branch *m_branch) {
        for(int index = 0; index < 2; index++) {
            this->m_rect.min_Rec[index] = std::min(this->m_rect.min_Rec[index], m_branch->m_rect.min_Rec[index]);
            this->m_rect.max_Rec[index] = std::max(this->m_rect.max_Rec[index], m_branch->m_rect.max_Rec[index]);
        }
    }
    // 按照 Branch.h 定义，依赖全局 dic_str
    void CalcuKeyWordRele(std::string& text, std::vector<std::string> dic_str);
    void CalcuKeyWordWeight(std::string &text, std::vector<std::string> dic_str);

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