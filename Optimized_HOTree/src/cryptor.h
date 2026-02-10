#ifndef CRYPTOR_H
#define CRYPTOR_H

// 保持 Crypto++ 头文件在最前面
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/osrng.h>
#include <cryptopp/filters.h>
#include <string>
// 最后包含你的自定义头文件
#include "define.h"
#include "Branch.h"

class Branch;
class Cryptor {
private:
    int L_; //Hierarchical level
    CryptoPP::SecByteBlock default_key;
    std::vector<CryptoPP::SecByteBlock> key_vec;
    CryptoPP::AutoSeededRandomPool prng;

public:
    Cryptor(int L);
    // void aes_encrypt(const std::string& plain, std::string& cipher);
    // void aes_decrypt(const std::string& cipher_full, std::string& plain);
    // std::string encrypt_element(const Branch& elem);
    // Branch decrypt_element(const std::string& cipher);

    std::string aes_encrypt(const std::string& plain, int i);
    std::string aes_decrypt(const std::string& cipher_full, int i);
    // std::string encrypt_element(const Branch& elem, int i);
    // Branch decrypt_element(const std::string& cipher, int i);
};

#endif // CRYPTOR_H