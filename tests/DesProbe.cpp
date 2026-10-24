// DesProbe.cpp - 临时探针：打印 DES 子密钥与标准向量结果，用于定位自检失败原因
#include <cstdio>
#include <cstring>

#include "Core/Des.h"

int main() {
    const char* keyHex = "133457799BBCDFF1";
    uint8_t key[8];
    auto hv = [](char c) -> int { return (c <= '9') ? (c - '0') : (c - 'A' + 10); };
    for (int i = 0; i < 8; ++i) key[i] = (uint8_t)((hv(keyHex[i * 2]) << 4) | hv(keyHex[i * 2 + 1]));

    uint64_t sk[16];
    dpk::Des::BuildSubkeys(key, sk);
    for (int i = 0; i < 16; ++i) printf("K%-2d = %012llX\n", i + 1, (unsigned long long)sk[i]);

    uint8_t plain[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    uint8_t out[8];
    dpk::Des::EncryptBlock(sk, plain, out);
    printf("E = ");
    for (int i = 0; i < 8; ++i) printf("%02X", out[i]);
    printf("\n");
    printf("SelfTest = %d\n", dpk::Des::SelfTest() ? 1 : 0);
    return 0;
}