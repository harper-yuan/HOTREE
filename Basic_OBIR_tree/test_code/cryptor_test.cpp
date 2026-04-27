#define BOOST_TEST_MODULE cryptor
#include <boost/test/included/unit_test.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <map>
#include <tuple>
#include <limits>
#include <random>
#include <set> // 确保包含 set
#include <tree.h>        // 引入修复后的 tree.h
#include <cryptor.h>
#include <OHT.h>
#include <client.h>

BOOST_AUTO_TEST_SUITE(cryptor_correctness_test)
BOOST_AUTO_TEST_CASE(test_cryptor_correctness) {
    // -------------------------------------------------------------------------
    // 1. 初始化环境
    // -------------------------------------------------------------------------
    int L = 5; // 假设层级为 5
    Cryptor cryptor(L);
    std::cout << "--- Starting Cryptor Test ---" << std::endl;

    // -------------------------------------------------------------------------
    // 2. 测试基础字符串加解密 (aes_encrypt / aes_decrypt)
    // -------------------------------------------------------------------------
    std::string original_text = "This is a secret message checking AES-CTR correctness.";
    std::string cipher_text;
    std::string recovered_text;

    // 加密
    cryptor.aes_encrypt(original_text, cipher_text);

    // 断言：密文不应为空，且不应等于明文
    BOOST_CHECK(!cipher_text.empty());
    BOOST_CHECK(cipher_text != original_text);
    // AES-CTR 密文长度通常等于 IV长度 + 明文长度
    // Crypto++ AES blocksize 是 16 字节
    BOOST_CHECK_EQUAL(cipher_text.length(), 16 + original_text.length());

    // 解密
    cryptor.aes_decrypt(cipher_text, recovered_text);

    // 断言：解密后内容必须与原文完全一致
    BOOST_CHECK_EQUAL(original_text, recovered_text);
    std::cout << "[Pass] Basic String Encryption/Decryption" << std::endl;

    // -------------------------------------------------------------------------
    // 3. 测试带层级索引的加解密 (aes_encrypt with index)
    // -------------------------------------------------------------------------
    int key_index = 2; // 测试使用第 3 个密钥
    std::string cipher_text_idx;
    std::string recovered_text_idx;

    // 使用特定 Key 加密
    cipher_text_idx = cryptor.aes_encrypt(original_text, key_index);
    
    // 断言：不同 Key (default vs key[2]) 产生的密文应该不同
    // 注意：即便 Key 相同，由于 IV 随机，密文也本该不同，这里主要测试流程通畅
    BOOST_CHECK(cipher_text != cipher_text_idx);

    // 使用特定 Key 解密
    recovered_text_idx = cryptor.aes_decrypt(cipher_text_idx, key_index);

    // 断言：解密成功
    BOOST_CHECK_EQUAL(original_text, recovered_text_idx);
    
    // 交叉测试：用错误的 Key 解密应该失败（乱码）
    std::string wrong_recovery;
    wrong_recovery = cryptor.aes_decrypt(cipher_text_idx, key_index + 1); // 用 index=3 解密 index=2 的密文
    BOOST_CHECK(original_text != wrong_recovery);
    
    std::cout << "[Pass] Indexed Key Encryption/Decryption" << std::endl;

    // -------------------------------------------------------------------------
    // 4. 测试 Branch 对象加解密 (encrypt_element)
    // ⚠️ 注意：由于当前代码使用 memcpy，仅测试 int/double 字段
    // -------------------------------------------------------------------------
    Branch b_original;
    b_original.id = 10086;
    b_original.level = 1;
    b_original.is_empty_data = false;
    // 设置矩形坐标
    b_original.m_rect.min_Rec[0] = 10.5;
    b_original.m_rect.min_Rec[1] = 20.5;
    b_original.m_rect.max_Rec[0] = 30.5;
    b_original.m_rect.max_Rec[1] = 40.5;

    // 序列化并加密
    std::string branch_cipher = cryptor.encrypt_element(b_original);
    
    // 解密并反序列化
    Branch b_recovered = cryptor.decrypt_element(branch_cipher);

    // 断言：数值类型字段应完美恢复
    BOOST_CHECK_EQUAL(b_original.id, b_recovered.id);
    BOOST_CHECK_EQUAL(b_original.level, b_recovered.level);
    BOOST_CHECK_EQUAL(b_original.is_empty_data, b_recovered.is_empty_data);
    
    // 断言：浮点数比较
    double epsilon = 1e-6;
    BOOST_CHECK_CLOSE(b_original.m_rect.min_Rec[0], b_recovered.m_rect.min_Rec[0], epsilon);
    BOOST_CHECK_CLOSE(b_original.m_rect.max_Rec[1], b_recovered.m_rect.max_Rec[1], epsilon);

    std::cout << "[Pass] Branch Element Encryption (POD fields)" << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()