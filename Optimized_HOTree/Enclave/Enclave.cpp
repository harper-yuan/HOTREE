/*
 * Copyright (C) 2011-2021 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdarg.h>
#include <stdio.h>      /* vsnprintf */

#include "Enclave.h"
#include "Enclave_t.h"  /* print_string */

#include "Enclave_t.h"  // SGX 自动生成的
#include "sgx_trts.h"   // 包含 sgx_is_outside_enclave, sgx_read_rand
#include "sgx_tcrypto.h" // 包含 SGX 加密库
#include <vector>
#include <cstring>
/* 
 * printf: 
 *   Invokes OCALL to display the enclave buffer to the terminal.
 */
void printf(const char *fmt, ...)
{
    char buf[BUFSIZ] = {'\0'};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, BUFSIZ, fmt, ap);
    va_end(ap);
    ocall_print_string(buf);
}

static sgx_aes_gcm_128bit_key_t g_global_key = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

sgx_aes_gcm_128bit_key_t* key = &g_global_key;
// sgx_aes_gcm_128bit_key_t* key = new sgx_aes_gcm_128bit_key_t [0];
void ecall_aes_encrypt(const char* plain, size_t len, char* cipher) {


    // 设置AES加密的IV
    uint8_t iv[SGX_AESGCM_IV_SIZE] = {0}; // 你应该使用一个安全的随机数生成器来生成IV

    // 加密操作
    sgx_status_t status = sgx_rijndael128GCM_encrypt(key, (const uint8_t*)plain, len, (uint8_t*)cipher, iv, sizeof(iv), NULL, 0, NULL);
    if (status != SGX_SUCCESS) {
        // 加密失败处理
        return;
    }

}

void ecall_aes_decrypt(const char* cipher, size_t len, char* plain) 
{
    uint8_t iv[SGX_AESGCM_IV_SIZE] = {0}; // 这里应该使用与加密时相同的IV

    // 解密操作
    sgx_status_t status = sgx_rijndael128GCM_decrypt(key, (const uint8_t*)cipher, len, (uint8_t*)plain, iv, sizeof(iv), NULL, 0, NULL);

    if (status != SGX_SUCCESS) {
        // 解密失败处理，可以记录日志或者抛出异常
        return;
    }


    return;
}