#ifndef CRYPTOR_H
#define CRYPTOR_H

#include <string>
#include <vector>
#include "define.h"
#include "Branch.h" 

class Cryptor {
private:
    int L_; // Hierarchical level

public:
    Cryptor(int L);

    // 基础加解密接口，调用 SGX ECALL
    // 参数 i 在此处主要为了兼容接口，实际密钥由 Enclave 管理
    std::string aes_encrypt(const std::string& plain, int i = 0);
    std::string aes_decrypt(const std::string& cipher_full, int i = 0);

    // 针对 Branch 对象的序列化与加密接口
    std::string encrypt_element(const Branch& elem, int i = 0);
    Branch decrypt_element(const std::string& cipher, int i = 0);
};

#endif // CRYPTOR_H