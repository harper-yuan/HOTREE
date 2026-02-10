/* Enclave/TrustedLibrary/TCryptor.h */
#pragma once
#include <sgx_tcrypto.h>
#include <vector>
#include <string>

class TCryptor {
private:
    std::vector<sgx_aes_gcm_128bit_key_t> keys; // 模拟多层级密钥
    sgx_aes_gcm_128bit_key_t default_key;

public:
    TCryptor(int L);
    // 保持接口跟原来类似
    std::string aes_encrypt(const std::string& plain, int level);
    std::string aes_decrypt(const std::string& cipher, int level);
};

/* Enclave/TrustedLibrary/TCryptor.cpp */
#include "TCryptor.h"
#include <sgx_trts.h>
#include <cstring>

TCryptor::TCryptor(int L) {
    keys.resize(L + 1);
    // 生成随机密钥
    sgx_read_rand((uint8_t*)&default_key, 16);
    for(int i=0; i<=L; i++) {
        sgx_read_rand((uint8_t*)&keys[i], 16);
    }
}

std::string TCryptor::aes_encrypt(const std::string& plain, int level) {
    if(plain.empty()) return "";
    
    // 生成随机 IV
    uint8_t ctr[16];
    sgx_read_rand(ctr, 16);
    
    std::string cipher = plain; // 拷贝明文空间
    uint32_t ctr_inc_bits = 128;
    const auto* key = (level == -1) ? &default_key : &keys[level];

    // SGX AES-CTR 加密
    sgx_aes_ctr_encrypt(key, (const uint8_t*)plain.data(), plain.size(), ctr, ctr_inc_bits, (uint8_t*)&cipher[0]);

    // 返回 IV + 密文 (这是常规做法，或者你自己定协议)
    return std::string((char*)ctr, 16) + cipher;
}

std::string TCryptor::aes_decrypt(const std::string& cipher_full, int level) {
    if(cipher_full.size() <= 16) return "";
    
    uint8_t ctr[16];
    memcpy(ctr, cipher_full.data(), 16); // 提取 IV
    
    std::string cipher_data = cipher_full.substr(16);
    std::string plain = cipher_data;
    uint32_t ctr_inc_bits = 128;
    const auto* key = (level == -1) ? &default_key : &keys[level];

    sgx_aes_ctr_decrypt(key, (const uint8_t*)cipher_data.data(), cipher_data.size(), ctr, ctr_inc_bits, (uint8_t*)&plain[0]);
    return plain;
}