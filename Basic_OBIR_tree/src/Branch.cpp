#include "Branch.h"
using namespace std;

Branch::Branch() {
    counter_for_lastest_data = 0;
    for (int i = 0; i < 2; i++) {
        m_rect.max_Rec[i] = 0;
        m_rect.min_Rec[i] = 0;
    }
    // weight 初始化稍后进行，根据字典大小
    level = 0;
    is_empty_data = false;
    is_dummy_for_shuffle = false;
    id = -1;
    partent = NULL;
    trueData = padZero(trueData);
    text = "";
}

Branch::Branch(bool dummy) {
    counter_for_lastest_data = 0;
    is_dummy_for_shuffle = false;
    for (int i = 0; i < 2; i++) {
        m_rect.max_Rec[i] = 0;
        m_rect.min_Rec[i] = 0;
    }
    // weight 初始化稍后进行，根据字典大小
    level = 0;
    is_empty_data = dummy;
    id = -1;
    partent = NULL;
    trueData = padZero(trueData);
    text = "";
}

Branch::Branch(bool empty_data, bool dummy_for_shuffle) {
    counter_for_lastest_data = 0;
    for (int i = 0; i < 2; i++) {
        m_rect.max_Rec[i] = 0;
        m_rect.min_Rec[i] = 0;
    }
    // weight 初始化稍后进行，根据字典大小
    level = 0;
    is_empty_data = empty_data;
    is_dummy_for_shuffle = dummy_for_shuffle;
    id = -1;
    partent = NULL;
    trueData = padZero(trueData);
    text = "";
}

// 实现 1：从指针构造新对象
Branch::Branch(const Branch* other) {
    if (other == nullptr) return;

    // 复制所有基本成员
    this->id = other->id;
    this->level = other->level;
    this->counter_for_lastest_data = other->counter_for_lastest_data;
    this->is_empty_data = other->is_empty_data;
    this->is_dummy_for_shuffle = other->is_dummy_for_shuffle;
    
    // 复制复合对象 (std::string 和 std::vector 的赋值运算符会自动进行深拷贝)
    this->text = other->text;
    this->trueData = other->trueData;
    this->weight = other->weight;
    this->m_rect = other->m_rect;

    // 2. 关键点：为 child_triple 创建全新的指针
    this->child_triple.clear();
    for (const auto* old_triple : other->child_triple) {
        if (old_triple) {
            // new 出一个新的 Triple 对象，保证地址完全独立
            this->child_triple.push_back(new Triple(old_triple));
        }
    }
    // 注意：父节点和子节点的指针处理
    // 通常在复制单个节点时，我们不希望简单的复制指针地址（那还是指向同一个地方）
    // 这里的逻辑根据你的业务需求决定：
    this->partent = other->partent; 
    this->child_branch = other->child_branch; // 这里存的是指针，依然指向原来的子节点

    // 【新增修改】关键点：复制子节点的摘要信息
    // 这两个 vector 存储的是对象（Rectangle）和数据（vector<double>），
    // 这里的直接赋值会自动执行深拷贝，不需要写循环。
    this->child_rects = other->child_rects; 
    this->child_weights_vec = other->child_weights_vec;
}

Branch::~Branch() {
    for (auto* t : child_triple) {
        if (t) delete t;
    }
    child_triple.clear();
    
    child_branch.clear(); 
    // 注意：child_branch 只是指针引用，通常不需要在这里删除，
    // 除非 Branch 拥有 child_branch 的绝对所有权。
    // 根据你的 Build 逻辑，all_branchs 负责管理 Branch 本身，所以这里不动 child_branch。
}

bool Branch::operator<(const Branch& other) const {
    double x1 = (m_rect.min_Rec[0] + m_rect.max_Rec[0]) / 2;
    // double y1 = (m_rect.min_Rec[1] + m_rect.max_Rec[1]) / 2;
    double x2 = (other.m_rect.min_Rec[0] + other.m_rect.max_Rec[0]) / 2;
    // double y2 = (other.m_rect.min_Rec[1] + other.m_rect.max_Rec[1]) / 2;
    
    // 简单的 X 轴优先比较，用于 STR
    return x1 < x2;
}

void Branch::textUpdate(Branch* mbranch) {
    this->text = "flag";
}

void Branch::LowerText(string &text) {
    for(char &c : text) {
        c = tolower(c);
    }
}

int Branch::levenshteinDistance(const string& s1, const string& s2) {
    int m = s1.length();
    int n = s2.length();
    vector<vector<int>> dp(m + 1, vector<int>(n + 1, 0));
    for (int i = 1; i <= m; ++i) {
        for (int j = 1; j <= n; ++j) {
            int cost = (s1[i - 1] != s2[j - 1]);
            dp[i][j] = min({ dp[i - 1][j] + 1, dp[i][j - 1] + 1, dp[i - 1][j - 1] + cost });
        }
    }
    return dp[m][n];
}

double Branch::similarity(const string& s1, const string& s2) {
    int distance = levenshteinDistance(s1, s2);
    return 1.0 - static_cast<double>(distance) / max((int)max(s1.length(), s2.length()), 1);
}

void Branch::CalcuKeyWordRele(string& text, vector<string> dic_str) {
    this->text = text;
    if (weight.size() != dic_str.size()) {
        weight.resize(dic_str.size(), 0.0);
    }
    for (int i = 0; i < dic_str.size(); i++) {
        this->weight[i] = similarity(text, dic_str[i]);
    }
}

void Branch::CalcuKeyWordWeight(string &text, vector<string> dic_str) {
    // 简单的 TF 计算
    int dic_num = int(dic_str.size());
    vector<double> w(dic_num, 0);
    istringstream input(text);
    string temp;
    vector<string> tempstr;
    while(input >> temp) tempstr.push_back(temp);

    int i = 0;
    double maxnum = 0;
    for(string str1: dic_str) {
        LowerText(str1);
        for(string str2 : tempstr) {
            LowerText(str2);
            if(str1 == str2) {
                w[i]++;
            }
        }
        if(w[i] > maxnum) maxnum = w[i];
        i++;
    }

    if (maxnum > 0) {
        for(int j = 0; j < w.size(); j++) w[j] /= maxnum;
    }
    this->weight = w;
}

void Branch::keyWeightUpdate(Branch *nBranch) {
    if (this->weight.size() < nBranch->weight.size()) {
        this->weight.resize(nBranch->weight.size(), 0.0);
    }
    for(int index = 0; index < this->weight.size(); index++) {
        this->weight[index] = max(this->weight[index], nBranch->weight[index]);
    }
}

void Branch::rectUpdate(Rectangle *nRect) {
    for(int index = 0; index < 2; index++) {
        this->m_rect.min_Rec[index] = min(this->m_rect.min_Rec[index], nRect->min_Rec[index]);
        this->m_rect.max_Rec[index] = max(this->m_rect.max_Rec[index], nRect->max_Rec[index]);
    }
}

bool Branch::operator==(const Branch& other) const {
    return id == other.id;
}