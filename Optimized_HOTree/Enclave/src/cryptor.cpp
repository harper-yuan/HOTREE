#include "cryptor.h"
#include <cstring>
#include <vector>
#include "sgx_tcrypto.h" // 使用 SGX 内部加密库
#include "sgx_trts.h"    // 使用 sgx_read_rand

// 必须移除对 Enclave_u.h 和 global_eid 的引用
// #include "../App/Enclave_u.h" <--- 删除
// extern sgx_enclave_id_t global_eid; <--- 删除

// 定义一个内部使用的 AES Key (实际使用中应从安全途径获取)
static const sgx_aes_gcm_128bit_key_t g_internal_key = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

// 辅助打印函数（解决 cerr 报错）
// extern void printf(const char *fmt, ...);

Cryptor::Cryptor(int L) {
    L_ = L;
}

std::string Cryptor::aes_encrypt(const std::string& plain, int i)
{
    if (plain.empty()) return "";

    size_t len = plain.length();
    
    // 1. 生成 12 字节 IV
    uint8_t iv[12] = {0}; 
    sgx_read_rand(iv, 12);

    // 2. 准备输出容器：IV (12) + 密文 (len) + MAC (16)
    // SGX GCM 加密会生成 16 字节的 MAC tag
    std::string cipher_out;
    cipher_out.resize(12 + len + 16); 

    uint8_t* p_iv = (uint8_t*)&cipher_out[0];
    uint8_t* p_dst = (uint8_t*)&cipher_out[12];
    uint8_t* p_mac = (uint8_t*)&cipher_out[12 + len];

    // 拷贝生成的 IV 到输出头部
    memcpy(p_iv, iv, 12);

    // 3. 执行加密
    // 注意：在 Enclave 内部直接调用 sgx_rijndael128GCM_encrypt
    sgx_status_t status = sgx_rijndael128GCM_encrypt(
        &g_internal_key,             // 密钥
        (const uint8_t*)plain.data(),// 输入明文
        len,                         // 明文长度
        p_dst,                       // 输出密文
        iv,                          // IV
        12,                          // IV 长度
        NULL, 0,                     // AAD (附加验证数据)，这里为空
        (sgx_aes_gcm_128bit_tag_t*)p_mac // 输出 MAC Tag
    );

    if (status != SGX_SUCCESS) {
        // printf("Encryption failed with error code: %x\n", status);
        return "";
    }

    return cipher_out;
}

std::string Cryptor::aes_decrypt(const std::string& cipher_full, int i)
{   
    if(i == -1) return cipher_full;

    // 最小长度检查：IV(12) + MAC(16) = 28 字节
    if (cipher_full.length() < 28) {
        // printf("Decryption error: Ciphertext too short\n");
        return ""; 
    }

    size_t len = cipher_full.length() - 28;
    
    std::string plain;
    plain.resize(len);

    const uint8_t* p_iv = (const uint8_t*)cipher_full.data();
    const uint8_t* p_src = (const uint8_t*)cipher_full.data() + 12;
    const uint8_t* p_mac = (const uint8_t*)cipher_full.data() + 12 + len;

    // 4. 执行解密
    sgx_status_t status = sgx_rijndael128GCM_decrypt(
        &g_internal_key,
        p_src,
        len,
        (uint8_t*)&plain[0],
        p_iv,
        12,
        NULL, 0,
        (const sgx_aes_gcm_128bit_tag_t*)p_mac
    );

    if (status != SGX_SUCCESS) {
        // printf("Decryption failed or MAC mismatch. Error: %x\n", status);
        return "";
    }

    return plain;
}

// 辅助函数：追加数据到 buffer
void append_to_buffer(std::string& buf, const void* data, size_t size) {
    buf.append((const char*)data, size);
}

// 辅助函数：从 buffer 读取数据
void read_from_buffer(const std::string& buf, size_t& offset, void* dest, size_t size) {
    if (offset + size > buf.size()) return; // 简单防越界
    memcpy(dest, buf.data() + offset, size);
    offset += size;
}

std::string Cryptor::encrypt_element(const Branch& elem, int i) {
    std::string buffer;
    
    // 序列化逻辑 (保持不变，只是为了适配 stringstream 去掉了流操作)
    append_to_buffer(buffer, &elem.id, sizeof(elem.id));
    append_to_buffer(buffer, &elem.level, sizeof(elem.level));
    append_to_buffer(buffer, &elem.is_empty_data, sizeof(elem.is_empty_data));
    append_to_buffer(buffer, &elem.m_rect, sizeof(elem.m_rect)); 

    size_t weight_size = elem.weight.size();
    append_to_buffer(buffer, &weight_size, sizeof(size_t));
    if (weight_size > 0) {
        append_to_buffer(buffer, elem.weight.data(), weight_size * sizeof(double));
    }

    size_t text_len = elem.text.size();
    append_to_buffer(buffer, &text_len, sizeof(size_t));
    if (text_len > 0) {
        append_to_buffer(buffer, elem.text.data(), text_len);
    }

    return aes_encrypt(buffer, i);
}

Branch Cryptor::decrypt_element(const std::string& cipher, int i) {
    std::string plain = aes_decrypt(cipher, i);
    Branch elem;
    if (plain.empty()) return elem;

    size_t offset = 0;
    // 反序列化
    read_from_buffer(plain, offset, &elem.id, sizeof(elem.id));
    read_from_buffer(plain, offset, &elem.level, sizeof(elem.level));
    read_from_buffer(plain, offset, &elem.is_empty_data, sizeof(elem.is_empty_data));
    read_from_buffer(plain, offset, &elem.m_rect, sizeof(elem.m_rect));

    size_t weight_size = 0;
    read_from_buffer(plain, offset, &weight_size, sizeof(size_t));
    if (weight_size > 0) {
        elem.weight.resize(weight_size);
        read_from_buffer(plain, offset, elem.weight.data(), weight_size * sizeof(double));
    }

    size_t text_len = 0;
    read_from_buffer(plain, offset, &text_len, sizeof(size_t));
    if (text_len > 0) {
        std::vector<char> temp_buf(text_len);
        read_from_buffer(plain, offset, temp_buf.data(), text_len);
        elem.text.assign(temp_buf.begin(), temp_buf.end());
    }

    return elem;
}