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
        if (line.empty()) continue;

        istringstream iss(line);
        vector<string> tokens;
        string temp;
        // 先将整行按空格拆分成所有单词（token）
        while (iss >> temp) {
            tokens.push_back(temp);
        }

        // 每一行必须至少包含：关键词(至少一个单词) + p1 + p2
        if (tokens.size() < 3) continue; 

        // 1. 提取坐标：最后两个元素一定是 p1 和 p2
        try {
            double p2 = stod(tokens.back());
            tokens.pop_back();
            double p1 = stod(tokens.back());
            tokens.pop_back();

            // 2. 提取关键词：剩下的所有元素重新拼接起来
            string original_text = "";
            for (size_t i = 0; i < tokens.size(); ++i) {
                original_text += tokens[i];
                if (i != tokens.size() - 1) original_text += " ";
            }

            DataRecord record;
            record.original_text = original_text;
            record.x_coord = p1;
            record.y_coord = p2;

            // --- 保持你原有的文本规范化逻辑 (固定长度为 5) ---
            string processed_text = original_text;
            // if (processed_text.size() > 5) {
            //     processed_text = processed_text.substr(0, 5);
            // } else if (processed_text.size() < 5) {
            //     processed_text += string(5 - processed_text.size(), 'X');
            // }
            
            record.processed_text = processed_text;
            record.id = data_ID++;
            
            records.push_back(record);
            read_count++;
        } catch (...) {
            // 如果 stod 转换失败（例如格式不符），跳过此行
            continue;
        }
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
        if (line.empty()) continue;

        istringstream iss(line);
        vector<string> tokens;
        string temp;
        // 先将整行按空格拆分成所有单词（token）
        while (iss >> temp) {
            tokens.push_back(temp);
        }

        // 每一行必须至少包含：关键词(至少一个单词) + p1 + p2
        if (tokens.size() < 3) continue; 

        // 1. 提取坐标：最后两个元素一定是 p1 和 p2
        try {
            double p2 = stod(tokens.back());
            tokens.pop_back();
            double p1 = stod(tokens.back());
            tokens.pop_back();

            // 2. 提取关键词：剩下的所有元素重新拼接起来
            string original_text = "";
            for (size_t i = 0; i < tokens.size(); ++i) {
                original_text += tokens[i];
                if (i != tokens.size() - 1) original_text += " ";
            }

            DataRecord record;
            record.original_text = original_text;
            record.x_coord = p1;
            record.y_coord = p2;

            // --- 保持你原有的文本规范化逻辑 (固定长度为 5) ---
            string processed_text = original_text;
            if (processed_text.size() > 5) {
                processed_text = processed_text.substr(0, 5);
            } else if (processed_text.size() < 5) {
                processed_text += string(5 - processed_text.size(), 'X');
            }
            
            record.processed_text = processed_text;
            record.id = data_ID++;
            
            records.push_back(record);
            read_count++;
        } catch (...) {
            // 如果 stod 转换失败（例如格式不符），跳过此行
            continue;
        }
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