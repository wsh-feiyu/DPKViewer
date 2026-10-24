// DesProbe3.cpp - 把 Des.cpp 直接包含进本 TU，逐轮打印中间值（临时探针，不参与交付）
#include <cstdio>

#include "../src/DPKViewer/Core/Des.cpp"

using namespace dpk;

static uint64_t FromHex(const char* h, int nbytes) {
    uint64_t v = 0;
    auto hv = [](char ch) -> int { return (ch <= '9') ? (ch - '0') : (ch - 'A' + 10); };
    for (int i = 0; i < nbytes; ++i) v = (v << 8) | (uint8_t)((hv(h[i * 2]) << 4) | hv(h[i * 2 + 1]));
    return v;
}

int main() {
    uint64_t key = FromHex("133457799BBCDFF1", 8);
    uint64_t k56 = PermuteFrom64(key, PC1, 56);
    printf("k56 = %014llX\n", (unsigned long long)k56);
    uint32_t c = (uint32_t)((k56 >> 28) & 0x0FFFFFFFu);
    uint32_t d = (uint32_t)(k56 & 0x0FFFFFFFu);
    printf("C0  = %07X  D0  = %07X\n", c, d);

    uint64_t blk = PermuteFrom64(FromHex("0123456789ABCDEF", 8), IP, 64);
    printf("IP  = %016llX\n", (unsigned long long)blk);
    uint32_t l = (uint32_t)(blk >> 32);
    uint32_t r = (uint32_t)(blk & 0xFFFFFFFFu);
    printf("L0  = %08X  R0  = %08X\n", l, r);

    uint8_t keyBytes[8];
    for (int i = 0; i < 8; ++i) keyBytes[i] = (uint8_t)((key >> (56 - i * 8)) & 0xFF);
    uint64_t sk[16];
    Des::BuildSubkeys(keyBytes, sk);
    printf("K1  = %012llX  f(R0,K1) = %08X\n", (unsigned long long)sk[0], Feistel(r, sk[0]));
    uint32_t t = l ^ Feistel(r, sk[0]);
    l = r;
    r = t;
    printf("L1  = %08X  R1  = %08X\n", l, r);
    t = l ^ Feistel(r, sk[1]);
    l = r;
    r = t;
    printf("L2  = %08X  R2  = %08X  f(R1,K2) = %08X\n", l, r, Feistel(r, sk[1]));
    t = l ^ Feistel(r, sk[2]);
    l = r;
    r = t;
    printf("L3  = %08X  R3  = %08X\n", l, r);

    uint8_t in[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    uint8_t out[8];
    Des::EncryptBlock(sk, in, out);
    printf("E   = ");
    for (int i = 0; i < 8; ++i) printf("%02X", out[i]);
    printf("\n");
    return 0;
}