// DesProbe2.cpp - 逐轮打印 DES 中间值，用于与 Python 复算逐字节对照
#include <cstdio>

#include "Core/Des.h"

int main() {
    const char* keyHex = "133457799BBCDFF1";
    uint8_t key[8];
    auto hv = [](char c) -> int { return (c <= '9') ? (c - '0') : (c - 'A' + 10); };
    for (int i = 0; i < 8; ++i) key[i] = (uint8_t)((hv(keyHex[i * 2]) << 4) | hv(keyHex[i * 2 + 1]));

    uint64_t sk[16];
    dpk::Des::BuildSubkeys(key, sk);

    uint8_t plain[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    uint8_t out[8];
    dpk::Des::EncryptBlock(sk, plain, out);
    printf("E = ");
    for (int i = 0; i < 8; ++i) printf("%02X", out[i]);
    printf("\n");

    // 单块解密一致性
    uint8_t back[8];
    dpk::Des::DecryptBlock(sk, out, back);
    printf("D = ");
    for (int i = 0; i < 8; ++i) printf("%02X", back[i]);
    printf("\n");
    return 0;
}