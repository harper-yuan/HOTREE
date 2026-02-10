#include "cryptor.h"
#include <cstring> // for std::memcpy, std::memset

// 1. 【关键】包含自动生成的代理头文件
// 假设 cryptor.cpp 在 src/ 下，而生成的头文件在 App/ 下
#include "../App/Enclave_u.h"
#include "sgx_urts.h"

// 2. 【关键】声明全局 EID，这样才能告诉 SGX 发送给哪个 Enclave
// 这个变量是在 App/App.cpp 中定义的
extern sgx_enclave_id_t global_eid; 

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

// =========================================================
// AES Encrypt (忽略参数 i，调用无 ID 的 Enclave 接口)
// =========================================================
std::string Cryptor::aes_encrypt(const std::string& plain, int i)
{
    // 忽略 i，因为 Enclave 内只有一个密钥
    if (plain.empty()) return "";

    size_t data_len = plain.length();

    // 1. 准备输出容器 (Length + 16 bytes IV)
    std::string cipher;
    cipher.resize(data_len + 16);

    // 2. 调用 Enclave
    // 参数说明: eid, input_ptr, input_len, output_ptr
    ecall_aes_encrypt(global_eid, 
                      plain.data(), 
                      data_len, 
                      &cipher[0]);

    return cipher;
}

// =========================================================
// AES Decrypt (忽略参数 i)
// =========================================================
std::string Cryptor::aes_decrypt(const std::string& cipher_full, int i)
{   
    // 保留 Stash 处理逻辑 (-1 不处理)
    if(i == -1) { 
        return cipher_full;
    }

    if (cipher_full.length() < 16) {
        throw std::runtime_error("Ciphertext too short");
    }

    size_t data_len = cipher_full.length();
    size_t plain_len = data_len - 16;

    // 1. 准备输出容器
    std::string plain;
    plain.resize(plain_len);

    // 2. 调用 Enclave
    // 参数说明: eid, input_ptr, input_len, output_ptr
    ecall_aes_decrypt(global_eid, 
                      cipher_full.data(), 
                      data_len, 
                      &plain[0]);

    return plain;
}

// void Cryptor::aes_encrypt(const std::string& plain, std::string& cipher)
// {
//     CryptoPP::SecByteBlock key = default_key;
//     CTR_Mode<AES>::Encryption encrypt_handler;

//     // 修改点 1: 去掉 CryptoPP::，直接用 unsigned char 或全局 byte
//     // 你的版本中 byte 是全局定义的，所以 CryptoPP::byte 是错的
//     unsigned char iv[AES::BLOCKSIZE]; 
//     {
//         prng.GenerateBlock(iv, AES::BLOCKSIZE);
//     }

//     encrypt_handler.SetKeyWithIV(key, key.size(), iv, AES::BLOCKSIZE);

//     std::string cipher_text_str;
//     cipher_text_str.resize(plain.length());
    
//     // 修改点 2: 强制转换使用 unsigned char* // Crypto++ 的 API 接受 byte*，而 byte 就是 unsigned char
//     encrypt_handler.ProcessData(
//         (unsigned char*)&cipher_text_str[0], 
//         (const unsigned char*)plain.data(), 
//         plain.length()
//     );

//     cipher = std::string((const char*)iv, AES::BLOCKSIZE) + cipher_text_str;
// }
// void Cryptor::aes_decrypt(const std::string& cipher_full, std::string& plain)
// {   
//     CryptoPP::SecByteBlock key = default_key;
//     if (cipher_full.length() < AES::BLOCKSIZE) return;

//     CTR_Mode<AES>::Decryption decrypt_handler;

//     // 修改点 3: 使用 unsigned char
//     unsigned char iv[AES::BLOCKSIZE];
    
//     std::memcpy(iv, cipher_full.data(), AES::BLOCKSIZE);

//     std::string cipher_data = cipher_full.substr(AES::BLOCKSIZE);
    
//     decrypt_handler.SetKeyWithIV(key, key.size(), iv, AES::BLOCKSIZE);

//     plain.resize(cipher_data.length());
    
//     // 修改点 4: 强制转换
//     decrypt_handler.ProcessData(
//         (unsigned char*)&plain[0], 
//         (const unsigned char*)cipher_data.data(), 
//         cipher_data.length()
//     );
// }

// // 辅助函数：将数据追加到 buffer
// void append_to_buffer(std::string& buf, const void* data, size_t size) {
//     buf.append((const char*)data, size);
// }

// // 辅助函数：从 buffer 读取数据
// void read_from_buffer(const std::string& buf, size_t& offset, void* dest, size_t size) {
//     if (offset + size > buf.size()) throw std::runtime_error("Buffer overflow");
//     std::memcpy(dest, buf.data() + offset, size);
//     offset += size;
// }

// std::string Cryptor::encrypt_element(const Branch& elem) {
    
//     std::string buffer;
    
//     // 1. 序列化 POD 数据 (基础类型)
//     append_to_buffer(buffer, &elem.id, sizeof(elem.id));
//     append_to_buffer(buffer, &elem.level, sizeof(elem.level));
//     append_to_buffer(buffer, &elem.is_empty_data, sizeof(elem.is_empty_data));
//     append_to_buffer(buffer, &elem.m_rect, sizeof(elem.m_rect)); // Rectangle 是 POD

//     // 2. 序列化 std::vector<double> weight
//     size_t weight_size = elem.weight.size();
//     append_to_buffer(buffer, &weight_size, sizeof(size_t)); // 先存大小
//     if (weight_size > 0) {
//         append_to_buffer(buffer, elem.weight.data(), weight_size * sizeof(double)); // 再存数据
//     }

//     // 3. 序列化 std::string text
//     size_t text_len = elem.text.size();
//     append_to_buffer(buffer, &text_len, sizeof(size_t)); // 先存长度
//     if (text_len > 0) {
//         append_to_buffer(buffer, elem.text.data(), text_len); // 再存字符
//     }

//     // 4. 加密序列化后的 buffer
//     std::string cipher;
//     aes_encrypt(buffer, cipher);
//     return cipher;
// }

// std::string Cryptor::encrypt_element(const Branch& elem, int i) {
//     std::string buffer;
    
//     // 1. 序列化 POD 数据 (基础类型)
//     append_to_buffer(buffer, &elem.id, sizeof(elem.id));
//     append_to_buffer(buffer, &elem.level, sizeof(elem.level));
//     append_to_buffer(buffer, &elem.is_empty_data, sizeof(elem.is_empty_data));
//     append_to_buffer(buffer, &elem.m_rect, sizeof(elem.m_rect)); // Rectangle 是 POD

//     // 2. 序列化 std::vector<double> weight
//     size_t weight_size = elem.weight.size();
//     append_to_buffer(buffer, &weight_size, sizeof(size_t)); // 先存大小
//     if (weight_size > 0) {
//         append_to_buffer(buffer, elem.weight.data(), weight_size * sizeof(double)); // 再存数据
//     }

//     // 3. 序列化 std::string text
//     size_t text_len = elem.text.size();
//     append_to_buffer(buffer, &text_len, sizeof(size_t)); // 先存长度
//     if (text_len > 0) {
//         append_to_buffer(buffer, elem.text.data(), text_len); // 再存字符
//     }

//     // 4. 加密序列化后的 buffer
//     std::string cipher = aes_encrypt(buffer, i);
//     return cipher;
// }

// Branch Cryptor::decrypt_element(const std::string& cipher) {
//     // 1. 解密
//     std::string plain;
//     aes_decrypt(cipher, plain);
    
//     Branch elem;

//     size_t offset = 0;

//     try {
//         // 2. 反序列化 POD 数据
//         read_from_buffer(plain, offset, &elem.id, sizeof(elem.id));
//         read_from_buffer(plain, offset, &elem.level, sizeof(elem.level));
//         read_from_buffer(plain, offset, &elem.is_empty_data, sizeof(elem.is_empty_data));
//         read_from_buffer(plain, offset, &elem.m_rect, sizeof(elem.m_rect));

//         // 3. 反序列化 std::vector<double> weight
//         size_t weight_size = 0;
//         read_from_buffer(plain, offset, &weight_size, sizeof(size_t));
//         if (weight_size > 0) {
//             elem.weight.resize(weight_size);
//             // 直接读取数据到 vector 的内存中
//             read_from_buffer(plain, offset, elem.weight.data(), weight_size * sizeof(double));
//         }

//         // 4. 反序列化 std::string text
//         size_t text_len = 0;
//         read_from_buffer(plain, offset, &text_len, sizeof(size_t));
//         if (text_len > 0) {
//             elem.text.resize(text_len);
//             // 字符串比较特殊，不能直接 memcpy 到 data() (C++17 前)，但 resize 后这样写通常兼容
//             // 更安全的做法是读取到临时 buffer
//             std::vector<char> temp_buf(text_len);
//             read_from_buffer(plain, offset, temp_buf.data(), text_len);
//             elem.text.assign(temp_buf.begin(), temp_buf.end());
//         }
//     } catch (const std::exception& e) {
//         std::cerr << "Deserialization error: " << e.what() << std::endl;
//         // 返回一个空对象或做其他处理
//     }

//     return elem;
// }

// Branch Cryptor::decrypt_element(const std::string& cipher, int i) {
//     // 1. 解密
//     std::string plain = aes_decrypt(cipher, i);
    
//     Branch elem;
//     // 初始化指针为空，防止随机值导致析构崩溃

//     size_t offset = 0;

//     try {
//         // 2. 反序列化 POD 数据
//         read_from_buffer(plain, offset, &elem.id, sizeof(elem.id));
//         read_from_buffer(plain, offset, &elem.level, sizeof(elem.level));
//         read_from_buffer(plain, offset, &elem.is_empty_data, sizeof(elem.is_empty_data));
//         read_from_buffer(plain, offset, &elem.m_rect, sizeof(elem.m_rect));

//         // 3. 反序列化 std::vector<double> weight
//         size_t weight_size = 0;
//         read_from_buffer(plain, offset, &weight_size, sizeof(size_t));
//         if (weight_size > 0) {
//             elem.weight.resize(weight_size);
//             // 直接读取数据到 vector 的内存中
//             read_from_buffer(plain, offset, elem.weight.data(), weight_size * sizeof(double));
//         }

//         // 4. 反序列化 std::string text
//         size_t text_len = 0;
//         read_from_buffer(plain, offset, &text_len, sizeof(size_t));
//         if (text_len > 0) {
//             elem.text.resize(text_len);
//             // 字符串比较特殊，不能直接 memcpy 到 data() (C++17 前)，但 resize 后这样写通常兼容
//             // 更安全的做法是读取到临时 buffer
//             std::vector<char> temp_buf(text_len);
//             read_from_buffer(plain, offset, temp_buf.data(), text_len);
//             elem.text.assign(temp_buf.begin(), temp_buf.end());
//         }
//     } catch (const std::exception& e) {
//         std::cerr << "Deserialization error: " << e.what() << std::endl;
//         // 返回一个空对象或做其他处理
//     }

//     return elem;
// }