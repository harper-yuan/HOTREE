#include "tree.h"
#include <random>

using namespace std;
// --- 全局变量定义 (对应 PlainBranch.h 的依赖) ---
vector<string> dic_str;
map<string, int> dic_map;

// ================= Rectangle 实现 =================

Rectangle::Rectangle() {
    min_Rec[0] = min_Rec[1] = numeric_limits<double>::max();
    max_Rec[0] = max_Rec[1] = numeric_limits<double>::lowest();
}

double Rectangle::Area() {
    double w = max_Rec[0] - min_Rec[0];
    double h = max_Rec[1] - min_Rec[1];
    if (w < 0 || h < 0) return 0.0;
    return w * h;
}

double Rectangle::MinDist(const Rectangle& pointRect) const {
    // 假设 pointRect 其实就是一个点（min=max），或者是一个极小的查询框
    // 我们计算当前矩形 (this) 到 pointRect 的最小欧氏距离
    double sum = 0.0;
    
    for (int i = 0; i < 2; i++) {
        double p_coord = pointRect.min_Rec[i]; // 查询点坐标
        double dist = 0.0;

        if (p_coord < this->min_Rec[i]) {
            dist = this->min_Rec[i] - p_coord;
        } else if (p_coord > this->max_Rec[i]) {
            dist = p_coord - this->max_Rec[i];
        } else {
            dist = 0.0; // 点在矩形在这个轴的范围内
        }
        sum += dist * dist;
    }
    return sqrt(sum);
}
// ================= PlainBranch 实现 =================

PlainBranch::PlainBranch() {
    textID = 0;
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
    pointBranch = NULL;
    childNode = NULL;
    trueData = padZero(trueData);
    text = "";
    curNode = NULL;
}

PlainBranch::PlainBranch(bool dummy) {
    textID = 0;
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
    pointBranch = NULL;
    childNode = NULL;
    trueData = padZero(trueData);
    text = "";
    curNode = NULL;
}

PlainBranch::PlainBranch(bool empty_data, bool dummy_for_shuffle) {
    textID = 0;
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
    pointBranch = NULL;
    childNode = NULL;
    trueData = padZero(trueData);
    text = "";
    curNode = NULL;
}

bool PlainBranch::operator<(const PlainBranch& other) const {
    double x1 = (m_rect.min_Rec[0] + m_rect.max_Rec[0]) / 2;
    // double y1 = (m_rect.min_Rec[1] + m_rect.max_Rec[1]) / 2;
    double x2 = (other.m_rect.min_Rec[0] + other.m_rect.max_Rec[0]) / 2;
    // double y2 = (other.m_rect.min_Rec[1] + other.m_rect.max_Rec[1]) / 2;
    
    // 简单的 X 轴优先比较，用于 STR
    return x1 < x2;
}

void PlainBranch::textUpdate(PlainBranch* mbranch) {
    this->text = "flag";
}

void PlainBranch::LowerText(string &text) {
    for(char &c : text) {
        c = tolower(c);
    }
}

int PlainBranch::levenshteinDistance(const string& s1, const string& s2) {
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

double PlainBranch::similarity(const string& s1, const string& s2) {
    int distance = levenshteinDistance(s1, s2);
    return 1.0 - static_cast<double>(distance) / max((int)max(s1.length(), s2.length()), 1);
}

void PlainBranch::CalcuKeyWordRele(string& text) {
    this->text = text;
    if (weight.size() != dic_str.size()) {
        weight.resize(dic_str.size(), 0.0);
    }
    for (int i = 0; i < dic_str.size(); i++) {
        this->weight[i] = similarity(text, dic_str[i]);
    }
}

void PlainBranch::CalcuKeyWordWeight(string &text) {
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

void PlainBranch::keyWeightUpdate(PlainBranch *nBranch) {
    if (this->weight.size() < nBranch->weight.size()) {
        this->weight.resize(nBranch->weight.size(), 0.0);
    }
    for(int index = 0; index < this->weight.size(); index++) {
        this->weight[index] = max(this->weight[index], nBranch->weight[index]);
    }
}

void PlainBranch::rectUpdate(Rectangle *nRect) {
    for(int index = 0; index < 2; index++) {
        this->m_rect.min_Rec[index] = min(this->m_rect.min_Rec[index], nRect->min_Rec[index]);
        this->m_rect.max_Rec[index] = max(this->m_rect.max_Rec[index], nRect->max_Rec[index]);
    }
}

bool PlainBranch::operator==(const PlainBranch& other) const {
    return id == other.id;
}

// ================= Node 实现 =================

Node::Node(int lvl) {
    id = 0;
    level_in_plain_tree = lvl;
    count = 0;
    parentNode = NULL;
    initRectangle();
}

int Node::randNum(int n) {
    // 简单随机数实现
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, n - 1);
    return dis(gen);
}

bool Node::IsInternalNode() { return (level_in_plain_tree > 0); }
bool Node::IsLeaf() { return (level_in_plain_tree == 0); }

void Node::SetLevel() {
    if (count != 0 && !mBranch.empty()) {
        this->level_in_plain_tree = this->mBranch[0]->level;
    }
}

void Node::setCount() {
    int _count = 0;
    for (size_t i = 0; i < mBranch.size(); i++) {
        if (mBranch[i]->is_empty_data != true) {
            _count++;
        } else {
            // break; // 明文构建时可能不连续，去掉 break
        }
    }
    this->count = _count;
}

void Node::rectUpdate(PlainBranch *m_branch) {
    for(int index = 0; index < 2; index++) {
        this->m_rect.min_Rec[index] = min(this->m_rect.min_Rec[index], m_branch->m_rect.min_Rec[index]);
        this->m_rect.max_Rec[index] = max(this->m_rect.max_Rec[index], m_branch->m_rect.max_Rec[index]);
    }
}

void Node::initRectangle() {
    for(int index = 0; index < 2; index++) {
        this->m_rect.min_Rec[index] = numeric_limits<double>::max();
        this->m_rect.max_Rec[index] = numeric_limits<double>::lowest();
    }
}

void Node::addVirtualBranch(int temp_ID, string Com) {
    // 明文简化版：直接添加 dummy branch
    for(int index = int(this->mBranch.size()); index < MAX_SIZE; index++) {
        PlainBranch* virtual_branch = new PlainBranch();
        virtual_branch->trueData = "DUMMY"; // 模拟密文
        virtual_branch->is_empty_data = true;
        virtual_branch->id = temp_ID;
        virtual_branch->textID = temp_ID;
        virtual_branch->text = Com;
        virtual_branch->weight.resize(dic_str.size(), 0.0);
        temp_ID++;
        this->mBranch.push_back(virtual_branch);
    }
    this->setCount();
}

void Node::Lower(string &text) {
    for(char &c : text) c = tolower(c);
}

double Node::CalcuTextRelevancy(vector<double> weight1, vector<double> weight2) {
    double rele = 0;
    double sum1 = 0, sum2 = 0;
    size_t n = min(weight1.size(), weight2.size());
    for(size_t i = 0; i < n; i++) {
        rele += weight1[i] * weight2[i];
        sum1 += weight1[i] * weight1[i];
        sum2 += weight2[i] * weight2[i];
    }
    if (sum1 == 0 || sum2 == 0) return 0.0;
    return rele / (sqrt(sum1) * sqrt(sum2));
}

double Node::CalcuSpaceIncrease(Rectangle pre, Rectangle newr) {
    Rectangle r;
    for(int index = 0; index < 2; index++) {
        r.max_Rec[index] = max(pre.max_Rec[index], newr.max_Rec[index]);
        r.min_Rec[index] = min(pre.min_Rec[index], newr.min_Rec[index]);
    }
    double area_pre = pre.Area();
    if (area_pre <= 1e-9) return r.Area();
    return (r.Area() - area_pre) / area_pre;
}

double Node::CalcuTestSPaceRele(PlainBranch *n1, PlainBranch *n2) {
        // n1 是树中的节点（可能是矩形，也可能是叶子点）
    // n2 是查询节点（Query Branch）

    // 1. 计算文本相关性 (Cosine Similarity 已经在 CalcuTextRelevancy 中处理)
    // 注意：Build 阶段 internal node 的 weight 已经是子节点的 max 汇总，
    // 所以这里的 text 分数是有效的 Upper Bound。
    double text = CalcuTextRelevancy(n1->weight, n2->weight);

    // 2. 计算空间距离 (使用 MinDist)
    // n1->m_rect 是数据矩形，n2->m_rect 是查询点
    double dist = n1->m_rect.MinDist(n2->m_rect);

    // 3. 归一化距离分数
    // 为了让距离越小分数越高，通常使用 1 / (1 + dist) 或者类似的高斯核
    // 假设 ALPHA 是权重参数 (0~1)
    
    // ⚠️ 注意：你需要确保 text 分数和 space 分数在同一个数量级，否则一个会主导另一个
    // text 通常是 0.0 ~ 1.0
    // 1.0 / (1.0 + dist) 也是 0.0 ~ 1.0
    
    double spaceScore = 1.0 / (1.0 + dist); 

    double rele = ALPHA * spaceScore + (1.0 - ALPHA) * text;
    
    return rele;
}

Rectangle Node::CombineRect(Rectangle *rect1, Rectangle *rect2) {
    Rectangle newRect;
    for(int index = 0; index < 2; index++) {
        newRect.min_Rec[index] = min(rect1->min_Rec[index], rect2->min_Rec[index]);
        newRect.max_Rec[index] = max(rect1->max_Rec[index], rect2->max_Rec[index]);
    }
    return newRect;
}

vector<double> Node::CombineKeyWords(vector<double> weight1, vector<double> weight2) {
    vector<double> newWeight(max(weight1.size(), weight2.size()), 0.0);
    for(size_t i = 0; i < min(weight1.size(), weight2.size()); i++) {
        newWeight[i] = max(weight1[i], weight2[i]);
    }
    return newWeight;
}

// ================= PlainIRTree 实现 =================

PlainIRTree::PlainIRTree(const vector<string>& dict) {
    // 初始化全局变量
    dic_str = dict;
    dic_map.clear();
    for (size_t i = 0; i < dict.size(); ++i) {
        dic_map[dict[i]] = (int)i;
    }
}

PlainIRTree::~PlainIRTree() {
    deleteTree(root);
}

void PlainIRTree::deleteTree(Node* node) {
    if (!node) return;
    if (node->level_in_plain_tree == 0) {
        for (auto* branch : node->mBranch) delete branch;
    } else {
        for (auto* branch : node->mBranch) {
            deleteTree(branch->childNode);
            delete branch; 
        }
    }
    delete node;
}

void PlainIRTree::Build(vector<DataRecord>& raw_data) {
    if (raw_data.empty()) return;

    id_to_record_map.clear();
    vector<PlainBranch*> position_branchs;
    int temp_ID = 0;

    // 1. 数据转换为 PlainBranch
    for (const auto& data : raw_data) {
        id_to_record_map[data.id] = data;

        PlainBranch* mBranch = new PlainBranch();
        mBranch->is_empty_data = false;
        mBranch->id = data.id;
        mBranch->textID = temp_ID;
        mBranch->trueData = to_string(data.id); // 模拟密文
        mBranch->text = data.processed_text;
        
        // 使用 PlainBranch 的计算方法，它会使用全局 dic_str
        // 这里模拟 "CalcuKeyWordWeight" (TF) 或 "CalcuKeyWordRele" (Similarity)
        // 按照 PlainBranch.h 的逻辑，CalcuKeyWordRele 是计算相似度，这里我们使用 TF 逻辑更符合一般检索
        // 但为了兼容结构体方法，我们调用:
        mBranch->CalcuKeyWordWeight(mBranch->text); //当前使用的是叶子节点，因此，他需要跟每个关键词计算相似度
        // mBranch->CalcuKeyWordRele(mBranch->text);

        mBranch->m_rect.min_Rec[0] = mBranch->m_rect.max_Rec[0] = data.x_coord;
        mBranch->m_rect.min_Rec[1] = mBranch->m_rect.max_Rec[1] = data.y_coord;
        mBranch->level = 0;
        mBranch->pointBranch = mBranch;
        mBranch->curNode = nullptr;

        position_branchs.push_back(mBranch);
        temp_ID++;
    }
    int data_num = position_branchs.size();

    // 3. STR 构建
    vector<Node*> nodes;
    
    // X 轴排序
    sort(position_branchs.begin(), position_branchs.begin() + data_num, 
         [](PlainBranch* a, PlainBranch* b) { return a->m_rect.min_Rec[0] < b->m_rect.min_Rec[0]; });

    int current_idx = 0;
    while (current_idx < position_branchs.size()) {//每次处理MAX_SIZE个数据
        int end_idx = min((size_t)(current_idx + MAX_SIZE), position_branchs.size());
        
        // Y 轴局部排序
        if (current_idx < data_num) {
             int sort_end = min(end_idx, data_num);
             sort(position_branchs.begin() + current_idx, position_branchs.begin() + sort_end,
                  [](PlainBranch* a, PlainBranch* b) { return a->m_rect.min_Rec[1] < b->m_rect.min_Rec[1]; });
        }

        Node* m_node = new Node(0);
        m_node->initRectangle(); // 初始化矩形
        //MAX_SIZE个数据放在一个节点m_node中，这个node包含这些branch的矩形
        for (int i = current_idx; i < end_idx; i++) {
            PlainBranch* b = position_branchs[i];
            b->curNode = m_node;
            m_node->mBranch.push_back(b);
            // ✅ 使用 Node.h 定义的 rectUpdate
            m_node->rectUpdate(b); 
        }
        
        m_node->setCount(); //记录有多少个真实节点
        m_node->id = current_idx / MAX_SIZE;
        nodes.push_back(m_node);
        current_idx = end_idx;
    }

    // 向上构建，构建父节点
    int _level = 1;
    while (nodes.size() > 1) {
        vector<Node*> parent_nodes; //记录当前层的节点向量
        int node_idx = 0;
        
        while (node_idx < nodes.size()) {
            Node* parent = new Node(_level); //一个节点存MAX_SIZE个branch
            parent->initRectangle();

            int pack_count = 0;
            while (pack_count < MAX_SIZE && node_idx < nodes.size()) { //遍历孩子节点，每MAX_SIZE变成一个新的branch
                Node* childNode = nodes[node_idx];
                
                PlainBranch* pBranch = new PlainBranch();
                pBranch->m_rect = childNode->m_rect; // 复制子节点 MBR
                pBranch->level = _level;
                pBranch->childNode = childNode;
                childNode->parentNode = parent;
                
                // 聚合权重
                pBranch->weight.resize(dic_str.size(), 0.0);
                for(auto* b : childNode->mBranch) {
                     if(!b->is_empty_data) pBranch->keyWeightUpdate(b);
                }

                pBranch->curNode = parent;
                parent->mBranch.push_back(pBranch);
                parent->rectUpdate(pBranch); // 更新父节点 MBR

                node_idx++;
                pack_count++;
            }
            parent->setCount();
            parent_nodes.push_back(parent);
        }
        nodes = parent_nodes;
        _level++;
    }

    if (!nodes.empty()) root = nodes[0];
}

vector<pair<double, DataRecord>> PlainIRTree::SearchTopK(double qx, double qy, string qText, int k) {
    vector<pair<double, DataRecord>> results;
    if (!root || k <= 0) return results;

    // 1. 初始化门槛分数（最小分数限制）
    // 初始值设为一个极小值，随着找到结果，它会不断提高
    double min_score = -1.0; 

    // 2. 构建查询节点
    PlainBranch* queryBranch = new PlainBranch();
    queryBranch->m_rect.min_Rec[0] = queryBranch->m_rect.max_Rec[0] = qx;
    queryBranch->m_rect.min_Rec[1] = queryBranch->m_rect.max_Rec[1] = qy;
    queryBranch->CalcuKeyWordWeight(qText);
    queryBranch->level = -1;

    // 3. 优先级队列（默认是大顶堆，分数最高的排在 top）
    std::priority_queue<PlainSearchItem> pq;

    // 4. 初始层入队
    for (auto* child : root->mBranch) {
        if (child->is_empty_data) continue;
        double score = root->CalcuTestSPaceRele(child, queryBranch); 
        pq.push({score, child});
    }

    // 5. 循环搜索
    while (!pq.empty()) {
        PlainSearchItem top = pq.top();
        pq.pop();

        // 【核心剪枝 1：出队截断】
        // 如果当前最大的分数已经小于当前的门槛分数，说明后面所有的节点都不可能入选
        if (results.size() >= k && top.score < min_score) {
            break; // 直接终止 while 循环，不需要清理 pq，因为后续元素只会更小
        }

        PlainBranch* curr = top.branch;

        if (curr->level == 0) {
            // 是叶子节点：存入结果
            if (id_to_record_map.count(curr->id)) {
                results.push_back(make_pair(top.score, id_to_record_map.at(curr->id)));
                
                // 【维护 min_score】
                // 只有当收集够了 k 个，才开始更新真正的 min_score
                if (results.size() >= k) {
                    // 在 Best-First 搜索中，第 k 个弹出的叶子节点的分数就是 Top-K 中的最小分
                    // 我们假设 results 是按分数从高到低存入的（BFS 的特性保证了这一点）
                    min_score = results.back().first;
                }
            }
            
            // 如果已经集齐 K 个，在 IR-tree 的 BFS 搜索中已经可以停止了
            if (results.size() >= k) break;

        } else {
            // 是中间节点：展开子节点
            Node* internalNode = curr->childNode;
            if (internalNode) {
                for (auto* child : internalNode->mBranch) {
                    if (child->is_empty_data) continue;
                    
                    double score = internalNode->CalcuTestSPaceRele(child, queryBranch);
                    
                    // 【核心剪枝 2：入队前过滤】
                    // 只有当该分支的“潜力分数”大于等于当前的门槛时，才允许入队
                    if (results.size() < k || score >= min_score) {
                        pq.push({score, child});
                    }
                }
            }
        }
    }

    delete queryBranch;
    return results;
}

// vector<pair<double, DataRecord>> PlainIRTree::SearchTopK(double qx, double qy, string qText, int k) {
//     vector<pair<double, DataRecord>> results;
//     int min_score = 10000;
//     if (!root || k <= 0) return results;

//     // 1. 构建查询节点
//     PlainBranch* queryBranch = new PlainBranch();
//     queryBranch->m_rect.min_Rec[0] = queryBranch->m_rect.max_Rec[0] = qx;
//     queryBranch->m_rect.min_Rec[1] = queryBranch->m_rect.max_Rec[1] = qy;
//     queryBranch->CalcuKeyWordWeight(qText);
//     queryBranch->level = -1;

//     // 2. 使用 priority_queue 替代 vector+sort
//     std::priority_queue<PlainSearchItem> pq;
//     // 3. 初始层入队
//     for (auto* child : root->mBranch) {
//         if (child->is_empty_data) continue;
        
//         // 【关键点】这里计算的是上界分数 (Upper Bound Score)
//         double score = root->CalcuTestSPaceRele(child, queryBranch); 
//         pq.push({score, child});
//     }

//     // 4. 循环搜索
//     while (!pq.empty() && results.size() < k) {
//         PlainSearchItem top = pq.top();
//         pq.pop(); // O(log N) 操作，比 erase 高效得多

//         PlainBranch* curr = top.branch;

//         // 【关键逻辑】检查是否已经到达叶子节点
//         if (curr->level == 0) {
//             // 是叶子节点：此时 top.score 是真实分数，直接加入结果
//             if (id_to_record_map.count(curr->id)) {
//                 results.push_back(make_pair(top.score, id_to_record_map.at(curr->id)));
//                 if(min_score > top.score) min_score = top.score;
//             }
//             //pruning
//             if(results.size() == k) {
//                 std::priority_queue<PlainSearchItem> next_pq;
//                 while (!pq.empty()) {
//                     if (pq.top().score >= min_score) {
//                         next_pq.push(pq.top());
//                     }
//                     pq.pop();
//                 }
//                 pq = std::move(next_pq);
//             }
//         } else {
//             // 是中间节点：展开子节点
//             Node* internalNode = curr->childNode;
//             if (internalNode) {
//                 for (auto* child : internalNode->mBranch) {
//                     if (child->is_empty_data) continue;
                    
//                     // 【关键点】
//                     // 必须确保 CalcuTestSPaceRele 对中间节点返回的是“乐观估计值”
//                     // 即：MinDist(空间) 和 MaxWeight(文本) 组合出的最高分
//                     double score = internalNode->CalcuTestSPaceRele(child, queryBranch);
//                     pq.push({score, child});
//                 }
//             }
//         }
//     }

//     delete queryBranch;
//     return results;
// }

vector<double> PlainIRTree::GetTextWeight(string text) {
    PlainBranch b;
    b.CalcuKeyWordWeight(text);
    return b.weight;
}