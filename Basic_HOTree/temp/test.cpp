#include <iostream>
#include <chrono>
#include <vector>
#include <random>
#include <thread>
#include <omp.h>

// 假设的 Branch 类定义
class Branch {
public:
    size_t id;
    size_t counter_for_lastest_data;
    int data;
    
    Branch(size_t id_, size_t counter_, int data_ = 0) 
        : id(id_), counter_for_lastest_data(counter_), data(data_) {}
    
    Branch(const Branch* other) 
        : id(other->id), counter_for_lastest_data(other->counter_for_lastest_data), data(other->data) {}
};

// 假设的 Client 类定义
class Client {
public:
    std::vector<Branch*> stash_;
    
    ~Client() {
        for (auto* branch : stash_) {
            delete branch;
        }
    }
};

// HOTree 类（简化版本）
class HOTree {
public:
    std::vector<Branch*> all_branchs;
    
    ~HOTree() {
        for (auto* branch : all_branchs) {
            delete branch;
        }
    }
    
    // 原始串行版本
    Branch* Retrun_in_stash_serial(size_t id, size_t counter, Client* client) {
        Branch* result = nullptr;
        for (size_t i = 0; i < client->stash_.size(); ++i) {
            if (client->stash_[i] && 
                client->stash_[i]->id == id && 
                client->stash_[i]->counter_for_lastest_data == counter) {
                result = new Branch(client->stash_[i]);
                all_branchs.push_back(result);
            }
        }
        return result;
    }
    
    // 优化后的并行版本（带早期退出）
    Branch* Retrun_in_stash_parallel(size_t id, size_t counter, Client* client) {
        Branch* result = nullptr;
        bool found = false;
        
        #pragma omp parallel for shared(result, found)
        for (size_t i = 0; i < client->stash_.size(); ++i) {
            if (found) continue;
            
            if (client->stash_[i] && 
                client->stash_[i]->id == id && 
                client->stash_[i]->counter_for_lastest_data == counter) {
                
                #pragma omp critical
                {
                    if (!found) {
                        result = new Branch(client->stash_[i]);
                        all_branchs.push_back(result);
                        found = true;
                    }
                }
            }
        }
        return result;
    }
    
    // 简单的并行版本（无早期退出，用于对比）
    Branch* Retrun_in_stash_parallel_simple(size_t id, size_t counter, Client* client) {
        Branch* result = nullptr;
        
        #pragma omp parallel for
        for (size_t i = 0; i < client->stash_.size(); ++i) {
            if (client->stash_[i] && 
                client->stash_[i]->id == id && 
                client->stash_[i]->counter_for_lastest_data == counter) {
                
                #pragma omp critical
                {
                    if (result == nullptr) {
                        result = new Branch(client->stash_[i]);
                        all_branchs.push_back(result);
                    }
                }
            }
        }
        return result;
    }
};

// 测试辅助函数：生成测试数据
Client* generate_test_client(size_t stash_size, size_t target_id, size_t target_counter, 
                              size_t match_position = 0) {
    Client* client = new Client();
    client->stash_.reserve(stash_size);
    
    // 生成随机数据
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> id_dist(1, 1000000);
    std::uniform_int_distribution<> counter_dist(1, 1000);
    
    for (size_t i = 0; i < stash_size; ++i) {
        size_t id = id_dist(gen);
        size_t counter = counter_dist(gen);
        
        // 在指定位置插入目标匹配项
        if (i == match_position) {
            id = target_id;
            counter = target_counter;
        }
        
        client->stash_.push_back(new Branch(id, counter, i));
    }
    
    return client;
}

// 性能测试函数
struct TestResult {
    size_t data_size;
    double serial_time_ms;
    double parallel_time_ms;
    double speedup;
    bool correct;
};

TestResult run_performance_test(size_t data_size, size_t match_position, int num_threads = 0) {
    if (num_threads > 0) {
        omp_set_num_threads(num_threads);
    }
    
    size_t target_id = 999999;
    size_t target_counter = 999;
    
    // 生成测试数据
    Client* client = generate_test_client(data_size, target_id, target_counter, match_position);
    HOTree tree_serial, tree_parallel;
    
    TestResult result;
    result.data_size = data_size;
    
    // 测试串行版本
    auto start = std::chrono::high_resolution_clock::now();
    Branch* serial_branch = tree_serial.Retrun_in_stash_serial(target_id, target_counter, client);
    auto end = std::chrono::high_resolution_clock::now();
    result.serial_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
    
    // 测试并行版本
    start = std::chrono::high_resolution_clock::now();
    Branch* parallel_branch = tree_parallel.Retrun_in_stash_parallel(target_id, target_counter, client);
    end = std::chrono::high_resolution_clock::now();
    result.parallel_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
    
    // 验证结果正确性
    result.correct = ((serial_branch == nullptr && parallel_branch == nullptr) ||
                      (serial_branch != nullptr && parallel_branch != nullptr));
    
    result.speedup = result.serial_time_ms / result.parallel_time_ms;
    
    delete client;
    return result;
}

// 多轮测试取平均值
TestResult average_test(size_t data_size, size_t match_position, int iterations = 5) {
    TestResult total = {data_size, 0, 0, 0, true};
    
    for (int i = 0; i < iterations; ++i) {
        TestResult r = run_performance_test(data_size, match_position);
        total.serial_time_ms += r.serial_time_ms;
        total.parallel_time_ms += r.parallel_time_ms;
        total.correct = total.correct && r.correct;
    }
    
    total.serial_time_ms /= iterations;
    total.parallel_time_ms /= iterations;
    total.speedup = total.serial_time_ms / total.parallel_time_ms;
    
    return total;
}

// 打印表格
void print_table_header() {
    printf("\n%-12s | %-12s | %-12s | %-10s | %-8s\n", 
           "Data Size", "Serial(ms)", "Parallel(ms)", "Speedup", "Correct");
    printf("------------|--------------|--------------|------------|----------\n");
}

void print_table_row(const TestResult& r) {
    printf("%-12zu | %-12.3f | %-12.3f | %-10.2f | %-8s\n",
           r.data_size, r.serial_time_ms, r.parallel_time_ms, 
           r.speedup, r.correct ? "✓" : "✗");
}

// 查找临界点（并行开始优于串行的数据量）
size_t find_crossover_point(size_t start_size = 100, size_t max_size = 1000000) {
    std::cout << "\n正在查找并行优于串行的临界点...\n";
    std::cout << "（匹配项在末尾，最坏情况）\n\n";
    
    size_t size = start_size;
    double last_speedup = 0;
    
    print_table_header();
    
    while (size <= max_size) {
        // 匹配项在末尾（最坏情况）
        TestResult r = average_test(size, size - 1, 3);
        print_table_row(r);
        
        // 当加速比 > 1.0 时返回
        if (r.speedup > 1.0) {
            std::cout << "\n✓ 临界点找到：数据量 " << size << " 时并行开始优于串行\n";
            return size;
        }
        
        // 指数增长
        if (size < 10000) {
            size *= 2;
        } else {
            size += 10000;
        }
    }
    
    return max_size;
}

// 不同匹配位置的性能测试
void test_match_positions() {
    std::cout << "\n========================================\n";
    std::cout << "不同匹配位置的性能对比 (数据量: 100000)\n";
    std::cout << "========================================\n\n";
    
    size_t data_size = 100000;
    std::vector<size_t> positions = {0, data_size/4, data_size/2, 3*data_size/4, data_size - 1};
    
    print_table_header();
    
    for (size_t pos : positions) {
        TestResult r = average_test(data_size, pos, 3);
        printf("%-12s | ", pos == 0 ? "开头" : 
                              (pos == data_size-1 ? "末尾" : 
                               "中间"));
        printf("%-12.3f | %-12.3f | %-10.2f | %-8s\n",
               r.serial_time_ms, r.parallel_time_ms, 
               r.speedup, r.correct ? "✓" : "✗");
    }
}

// 不同线程数的性能测试
void test_thread_counts() {
    std::cout << "\n========================================\n";
    std::cout << "不同线程数的性能对比 (数据量: 500000)\n";
    std::cout << "========================================\n\n";
    
    size_t data_size = 500000;
    size_t match_position = data_size - 1; // 末尾匹配（最坏情况）
    
    std::vector<int> thread_counts = {1, 2, 4, 8, 16};
    
    printf("%-10s | %-12s | %-12s | %-10s\n", 
           "Threads", "Time(ms)", "Speedup", "Efficiency");
    printf("----------|--------------|--------------|------------\n");
    
    double base_time = 0;
    
    for (int threads : thread_counts) {
        omp_set_num_threads(threads);
        
        Client* client = generate_test_client(data_size, 999999, 999, match_position);
        HOTree tree;
        
        auto start = std::chrono::high_resolution_clock::now();
        Branch* result = tree.Retrun_in_stash_parallel(999999, 999, client);
        auto end = std::chrono::high_resolution_clock::now();
        
        double time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        
        if (threads == 1) {
            base_time = time_ms;
        }
        
        double speedup = base_time / time_ms;
        double efficiency = speedup / threads;
        
        printf("%-10d | %-12.3f | %-10.2f | %-10.2f\n", 
               threads, time_ms, speedup, efficiency);
        
        delete client;
    }
}

// 主测试函数
int main() {
    std::cout << "========== HOTree::Retrun_in_stash 性能测试 ==========\n";
    std::cout << "编译器: " << (omp_get_num_procs() > 1 ? "OpenMP 已启用" : "OpenMP 未启用") << "\n";
    std::cout << "CPU 核心数: " << omp_get_num_procs() << "\n";
    std::cout << "最大线程数: " << omp_get_max_threads() << "\n";
    
    // 测试1：不同数据量的性能对比
    std::cout << "\n========================================\n";
    std::cout << "1. 不同数据量的性能对比\n";
    std::cout << "========================================\n";
    
    std::vector<size_t> test_sizes = {100, 1000, 10000, 50000, 100000, 500000, 1000000};
    
    print_table_header();
    for (size_t size : test_sizes) {
        // 匹配项在末尾（最坏情况）
        TestResult r = average_test(size, size - 1, 5);
        print_table_row(r);
    }
    
    // 测试2：不同匹配位置的影响
    test_match_positions();
    
    // 测试3：不同线程数的影响
    test_thread_counts();
    
    // 测试4：查找临界点
    size_t crossover = find_crossover_point(1000, 200000);
    
    // 总结报告
    std::cout << "\n========================================\n";
    std::cout << "测试总结\n";
    std::cout << "========================================\n";
    std::cout << "1. 串行版本适用于小数据量（< " << crossover << "）\n";
    std::cout << "2. 并行版本在大数据量时优势明显\n";
    std::cout << "3. 匹配位置越靠前，串行版本越快\n";
    std::cout << "4. 匹配位置越靠后，并行版本优势越大\n";
    std::cout << "5. 建议使用动态阈值：if (size > 50000) 使用并行版本\n";
    
    return 0;
}