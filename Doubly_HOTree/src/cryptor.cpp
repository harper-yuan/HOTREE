#include "cryptor.h"
#include <cstring> // for std::memcpy, std::memset

using namespace CryptoPP;

Cryptor::Cryptor(int L) {
    L_ = L;
    for(int i = 0; i <= L; i++) {
        CryptoPP::SecByteBlock key_temp;
        key_temp.resize(AES::DEFAULT_KEYLENGTH);
        prng.GenerateBlock(key_temp, key_temp.size());
        key_vec.push_back(key_temp);
    }
    default_key.resize(AES::DEFAULT_KEYLENGTH);
    prng.GenerateBlock(default_key, default_key.size());
}

void Cryptor::aes_encrypt(const std::string& plain, std::string& cipher)
{
    CryptoPP::SecByteBlock key = default_key;
    CTR_Mode<AES>::Encryption encrypt_handler;

    // 修改点 1: 去掉 CryptoPP::，直接用 unsigned char 或全局 byte
    // 你的版本中 byte 是全局定义的，所以 CryptoPP::byte 是错的
    unsigned char iv[AES::BLOCKSIZE]; 
    {
        prng.GenerateBlock(iv, AES::BLOCKSIZE);
    }

    encrypt_handler.SetKeyWithIV(key, key.size(), iv, AES::BLOCKSIZE);

    std::string cipher_text_str;
    cipher_text_str.resize(plain.length());
    
    // 修改点 2: 强制转换使用 unsigned char* // Crypto++ 的 API 接受 byte*，而 byte 就是 unsigned char
    encrypt_handler.ProcessData(
        (unsigned char*)&cipher_text_str[0], 
        (const unsigned char*)plain.data(), 
        plain.length()
    );

    cipher = std::string((const char*)iv, AES::BLOCKSIZE) + cipher_text_str;
}

std::string Cryptor::aes_encrypt(const std::string& plain, int i)
{
    return plain;
    // std::string plain = padZero(plain_no_pad);
    std::string cipher;
    CryptoPP::SecByteBlock key = key_vec[i];
    CTR_Mode<AES>::Encryption encrypt_handler;

    AutoSeededRandomPool local_prng; // 在栈上创建，线程安全
    unsigned char iv[AES::BLOCKSIZE]; 
    local_prng.GenerateBlock(iv, AES::BLOCKSIZE);

    encrypt_handler.SetKeyWithIV(key, key.size(), iv, AES::BLOCKSIZE);

    std::string cipher_text_str;
    cipher_text_str.resize(plain.length());
    
    // 修改点 2: 强制转换使用 unsigned char* // Crypto++ 的 API 接受 byte*，而 byte 就是 unsigned char
    encrypt_handler.ProcessData(
        (unsigned char*)&cipher_text_str[0], 
        (const unsigned char*)plain.data(), 
        plain.length()
    );
    cipher = std::string((const char*)iv, AES::BLOCKSIZE) + cipher_text_str;
    return cipher;
}

void Cryptor::aes_decrypt(const std::string& cipher_full, std::string& plain)
{   
    CryptoPP::SecByteBlock key = default_key;
    if (cipher_full.length() < AES::BLOCKSIZE) return;

    CTR_Mode<AES>::Decryption decrypt_handler;

    // 修改点 3: 使用 unsigned char
    unsigned char iv[AES::BLOCKSIZE];
    
    std::memcpy(iv, cipher_full.data(), AES::BLOCKSIZE);

    std::string cipher_data = cipher_full.substr(AES::BLOCKSIZE);
    
    decrypt_handler.SetKeyWithIV(key, key.size(), iv, AES::BLOCKSIZE);

    plain.resize(cipher_data.length());
    
    // 修改点 4: 强制转换
    decrypt_handler.ProcessData(
        (unsigned char*)&plain[0], 
        (const unsigned char*)cipher_data.data(), 
        cipher_data.length()
    );
}

std::string Cryptor::aes_decrypt(const std::string& cipher_full, int i)
{   
    return cipher_full;
    std::string plain;
    CryptoPP::SecByteBlock key = key_vec[i];
    if (cipher_full.length() < AES::BLOCKSIZE) {
        throw std::runtime_error(
            "Ciphertext too short: length " + std::to_string(cipher_full.length()) 
            + " < BLOCKSIZE " + std::to_string(AES::BLOCKSIZE)
        );
    }

    CTR_Mode<AES>::Decryption decrypt_handler;

    // 修改点 3: 使用 unsigned char
    unsigned char iv[AES::BLOCKSIZE];
    
    std::memcpy(iv, cipher_full.data(), AES::BLOCKSIZE);

    std::string cipher_data = cipher_full.substr(AES::BLOCKSIZE);
    
    decrypt_handler.SetKeyWithIV(key, key.size(), iv, AES::BLOCKSIZE);

    plain.resize(cipher_data.length());
    
    // 修改点 4: 强制转换
    decrypt_handler.ProcessData(
        (unsigned char*)&plain[0], 
        (const unsigned char*)cipher_data.data(), 
        cipher_data.length()
    );
    return plain;
}

// 辅助函数：将数据追加到 buffer
void append_to_buffer(std::string& buf, const void* data, size_t size) {
    buf.append((const char*)data, size);
}

// 辅助函数：从 buffer 读取数据
void read_from_buffer(const std::string& buf, size_t& offset, void* dest, size_t size) {
    if (offset + size > buf.size()) throw std::runtime_error("Buffer overflow");
    std::memcpy(dest, buf.data() + offset, size);
    offset += size;
}