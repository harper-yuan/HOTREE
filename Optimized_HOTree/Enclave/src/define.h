#pragma once
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <tuple>
#include <cmath>
#include <map>
#include <queue>
#include <limits>
#include <fstream>
#include <iomanip>
#include <random>
#include <cstring>
// #include <omp.h>

#ifndef MY_BLOCK_SIZE
  // 默认值
  constexpr const size_t BlockSize = 4096;
#else
  // 如果 CMake 传了 MY_BLOCK_SIZE，则将其值赋给 BlockSize 变量
  constexpr const size_t BlockSize = MY_BLOCK_SIZE;
#endif
// --- 1. 配置参数 ---
constexpr const int MAX_SIZE = 6;        // 节点最大容量 (分支数)
constexpr const double ALPHA = 0.5;      // 权重因子：0.5 表示空间和文本同等重要
constexpr const int Z = 512;            // Client stash size
constexpr const int num_users = 1;    
constexpr const int TEE_Z = num_users*Z;            
constexpr const size_t cuckoo_stash_size = 20;
// constexpr const size_t BlockSize = 4096; // padding every encrypted data to 4096 Bytes
constexpr const int num_threads = 8; 
constexpr const int debug_id = -337;
constexpr const int child_debug_id = -18;
constexpr const int global_seed = 200;
constexpr const int if_is_debug = 0;


// --- 2. 数据结构定义 ---
struct DataRecord {
    int id;               // 数据项ID
    std::string original_text; // 原始读取的文本
    std::string processed_text;  // 经过规范化 (截断或填充) 的文本
    double x_coord;       // X 坐标 (p1)
    double y_coord;       // Y 坐标 (p2)
};


// --- Rectangle 结构 (参考 PlainBranch.h) ---
struct Rectangle {
    double min_Rec[2];
    double max_Rec[2];

    Rectangle();
    double Area();
    bool operator==(const Rectangle& other) const { return false; }
    double MinDist(const Rectangle& other) const;
};

inline Rectangle::Rectangle() {
    min_Rec[0] = min_Rec[1] = std::numeric_limits<double>::max();
    max_Rec[0] = max_Rec[1] = std::numeric_limits<double>::lowest();
}

inline double Rectangle::Area() {
    double w = max_Rec[0] - min_Rec[0];
    double h = max_Rec[1] - min_Rec[1];
    if (w < 0 || h < 0) return 0.0;
    return w * h;
}

inline double Rectangle::MinDist(const Rectangle& pointRect) const {
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

static std::string padZero(const std::string& data) {
    if (data.size() > BlockSize) {
        throw std::invalid_argument("Data larger than target size");
    }
    
    std::string padded = data;
    padded.resize(BlockSize, '\0');
    return padded;
}

/**
 * 零填充解填充（去除尾部的零）
 * @param paddedData 填充后的数据
 * @return 原始数据
 */
static std::string unpadZero(const std::string& paddedData) {
    // 找到第一个零的位置
    size_t endPos = paddedData.find('\0');
    if (endPos == std::string::npos) {
        // 没有零，返回全部数据
        return paddedData;
    }
    
    // 检查后面的字节是否都是零
    for (size_t i = endPos; i < paddedData.size(); ++i) {
        if (paddedData[i] != '\0') {
            // 中间有非零字节，返回全部数据
            return paddedData;
        }
    }
    
    // 返回原始数据
    return paddedData.substr(0, endPos);
}

static uint64_t combine_unique(int id, int counter) {
    // 将 id 放在高 32 位，counter 放在低 32 位
    // 注意：必须先转成 uint64_t 否则位移会溢出
    return (static_cast<uint64_t>(id) << 32) | static_cast<uint32_t>(counter);
}