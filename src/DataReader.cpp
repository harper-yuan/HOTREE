#include "define.h"
using namespace std;
// --- 2. 核心数据读取函数 ---

/**
 * @brief 读取空间文本数据
 * * @param filename 数据文件路径
 * @param N 要读取的最大行数 (记录数)
 * @return vector<DataRecord> 包含读取和处理后的数据记录
 */
vector<DataRecord> readDataFromDataset(const string& filename, int N) {
    vector<DataRecord> records;
    ifstream inputFile;
    inputFile.open(filename);

    if (!inputFile.is_open()) {
        cerr << "error: can't open  the file: " << filename << endl;
        return records;
    }

    string line;
    int data_ID = 0; // 对应原始代码中的 temp_ID
    int read_count = 0;

    // 逐行读取，直到文件结束或达到 N 条记录的限制
    while (getline(inputFile, line) && read_count < N) {
        istringstream iss(line);
        DataRecord record;
        
        string text;
        double p1 = 0, p2 = 0;

        // 尝试解析一行数据，格式: text p1 p2
        if (!(iss >> text >> p1 >> p2)) {
            // 如果解析失败，可能是空行或格式错误，跳过
            continue; 
        }

        record.original_text = text;
        record.x_coord = p1;
        record.y_coord = p2;

        // --- 仿照 OBIR-tree 的文本规范化逻辑 (固定长度为 5) ---
        string processed_text;
        
        if (text.size() > 5) {
            // 截断到前 5 个字符
            processed_text = text.substr(0, 5);
        } else if (text.size() < 5) {
            // 填充 'X' 使长度达到 5 (对应原始代码中的 string(5 - text.size(), 'X'))
            processed_text = text + string(5 - text.size(), 'X');
        } else {
            // 长度正好为 5
            processed_text = text;
        }
        
        record.processed_text = processed_text;
        record.id = data_ID++;
        
        records.push_back(record);
        read_count++;
    }

    inputFile.close();
    // cout << "成功读取并处理了 " << read_count << " 条数据。" << endl;
    return records;
}

vector<DataRecord> readDataFromDataset(const string& filename) {
    vector<DataRecord> records;
    ifstream inputFile;
    inputFile.open(filename);

    if (!inputFile.is_open()) {
        cerr << "error: can't open  the file: " << filename << endl;
        return records;
    }

    string line;
    int data_ID = 0; // 对应原始代码中的 temp_ID
    int read_count = 0;

    // 逐行读取，直到文件结束或达到 N 条记录的限制
    while (getline(inputFile, line)) {
        istringstream iss(line);
        DataRecord record;
        
        string text;
        double p1 = 0, p2 = 0;

        // 尝试解析一行数据，格式: text p1 p2
        if (!(iss >> text >> p1 >> p2)) {
            // 如果解析失败，可能是空行或格式错误，跳过
            continue; 
        }

        record.original_text = text;
        record.x_coord = p1;
        record.y_coord = p2;

        // --- 仿照 OBIR-tree 的文本规范化逻辑 (固定长度为 5) ---
        string processed_text;
        
        if (text.size() > 5) {
            // 截断到前 5 个字符
            processed_text = text.substr(0, 5);
        } else if (text.size() < 5) {
            // 填充 'X' 使长度达到 5 (对应原始代码中的 string(5 - text.size(), 'X'))
            processed_text = text + string(5 - text.size(), 'X');
        } else {
            // 长度正好为 5
            processed_text = text;
        }
        
        record.processed_text = processed_text;
        record.id = data_ID++;
        
        records.push_back(record);
        read_count++;
    }

    inputFile.close();
    // cout << "成功读取并处理了 " << read_count << " 条数据。" << endl;
    return records;
}

vector<string> LoadDictionary(const string& filename) {
    vector<string> dict;
    ifstream file(filename);
    string word;
    while (getline(file, word)) {
        if (!word.empty()) dict.push_back(word);
    }
    return dict;
}