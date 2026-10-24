// Des.cpp - 标准 DES（FIPS 46-3）与 DPK 的 DES 变体实现
#include "Des.h"

#include <cstring>

namespace dpk {

namespace {

const int IP[64] = {58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4, 62, 54, 46, 38, 30, 22, 14, 6,
                    64, 56, 48, 40, 32, 24, 16, 8, 57, 49, 41, 33, 25, 17, 9,  1,  59, 51, 43, 35, 27, 19, 11, 3,
                    61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7};

const int FP[64] = {40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31, 38, 6, 46, 14, 54, 22, 62, 30,
                    37, 5, 45, 13, 53, 21, 61, 29, 36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27,
                    34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41, 9,  49, 17, 57, 25};

const int E[48] = {32, 1,  2,  3,  4,  5,  4,  5,  6,  7,  8,  9,  8,  9,  10, 11, 12, 13, 12, 13, 14, 15, 16, 17,
                   16, 17, 18, 19, 20, 21, 20, 21, 22, 23, 24, 25, 24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1};

const int P[32] = {16, 7,  20, 21, 29, 12, 28, 17, 1,  15, 23, 26, 5,  18, 31, 10,
                   2,  8,  24, 14, 32, 27, 3,  9,  19, 13, 30, 6,  22, 11, 4,  25};

const int PC1[56] = {57, 49, 41, 33, 25, 17, 9,  1,  58, 50, 42, 34, 26, 18, 10, 2,  59, 51, 43, 35,
                     27, 19, 11, 3,  60, 52, 44, 36, 63, 55, 47, 39, 31, 23, 15, 7,  62, 54, 46, 38,
                     30, 22, 14, 6,  61, 53, 45, 37, 29, 21, 13, 5,  28, 20, 12, 4};

const int PC2[48] = {14, 17, 11, 24, 1,  5,  3,  28, 15, 6,  21, 10, 23, 19, 12, 4,  26, 8,  16, 7,  27, 20, 13, 2,
                     41, 52, 31, 37, 47, 55, 30, 40, 51, 45, 33, 48, 44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32};

const int SHIFTS[16] = {1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1};

const uint8_t SBOX[8][64] = {
    {14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7, 0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
     4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0, 15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13},
    {15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10, 3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
     0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15, 13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9},
    {10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8, 13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
     13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7, 1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12},
    {7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15, 13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
     10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4, 3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14},
    {2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9, 14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
     4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14, 11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3},
    {12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11, 10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
     9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6, 4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13},
    {4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1, 13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
     1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2, 6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12},
    {13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7, 1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
     7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8, 2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11}};

inline uint64_t PermuteFrom64(uint64_t in, const int* table, int n) {
    uint64_t out = 0;
    for (int i = 0; i < n; ++i) out = (out << 1) | ((in >> (64 - table[i])) & 1ull);
    return out;
}

// 注意：输入为 32 位，但输出可能超过 32 位（E 扩展输出 48 位），
// 因此返回值必须是 64 位容器，否则高 16 位会被截断。
inline uint64_t PermuteFrom32(uint32_t in, const int* table, int n) {
    uint64_t out = 0;
    for (int i = 0; i < n; ++i) out = (out << 1) | ((in >> (32 - table[i])) & 1ull);
    return out;
}

inline uint64_t PermuteFrom56(uint64_t in, const int* table, int n) {
    uint64_t out = 0;
    for (int i = 0; i < n; ++i) out = (out << 1) | ((in >> (56 - table[i])) & 1ull);
    return out;
}

inline uint32_t Rotl28(uint32_t v, int c) { return ((v << c) | (v >> (28 - c))) & 0x0FFFFFFFu; }

inline uint64_t ReadBE64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}

inline void WriteBE64(uint8_t* p, uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        p[i] = (uint8_t)(v & 0xFF);
        v >>= 8;
    }
}

uint32_t Feistel(uint32_t r, uint64_t subkey) {
    uint64_t expanded = PermuteFrom32(r, E, 48);
    uint64_t x = (expanded ^ subkey) & 0xFFFFFFFFFFFFull;  // 48 位掩码
    uint32_t s = 0;
    for (int i = 0; i < 8; ++i) {
        int shift = 42 - i * 6;
        int group = (int)((x >> shift) & 0x3F);
        int row = ((group >> 5) & 1) << 1 | (group & 1);
        int col = (group >> 1) & 0x0F;
        s = (s << 4) | SBOX[i][row * 16 + col];
    }
    return (uint32_t)PermuteFrom32(s, P, 32);
}

void CryptBlock(const uint64_t subkeys[16], const uint8_t in[8], uint8_t out[8], bool decrypt) {
    uint64_t blk = PermuteFrom64(ReadBE64(in), IP, 64);
    uint32_t l = (uint32_t)(blk >> 32);
    uint32_t r = (uint32_t)(blk & 0xFFFFFFFFu);
    for (int i = 0; i < 16; ++i) {
        uint32_t t = l ^ Feistel(r, subkeys[decrypt ? 15 - i : i]);
        l = r;
        r = t;
    }
    uint64_t pre = ((uint64_t)r << 32) | (uint64_t)l;
    WriteBE64(out, PermuteFrom64(pre, FP, 64));
}

}  // namespace

void Des::BuildSubkeys(const uint8_t key8[8], uint64_t subkeys[16]) {
    uint64_t key = PermuteFrom64(ReadBE64(key8), PC1, 56);
    uint32_t c = (uint32_t)((key >> 28) & 0x0FFFFFFFu);
    uint32_t d = (uint32_t)(key & 0x0FFFFFFFu);
    for (int i = 0; i < 16; ++i) {
        c = Rotl28(c, SHIFTS[i]);
        d = Rotl28(d, SHIFTS[i]);
        uint64_t cd = ((uint64_t)c << 28) | (uint64_t)d;
        subkeys[i] = PermuteFrom56(cd, PC2, 48) & 0xFFFFFFFFFFFFull;
    }
}

void Des::EncryptBlock(const uint64_t subkeys[16], const uint8_t in[8], uint8_t out[8]) {
    CryptBlock(subkeys, in, out, false);
}

void Des::DecryptBlock(const uint64_t subkeys[16], const uint8_t in[8], uint8_t out[8]) {
    CryptBlock(subkeys, in, out, true);
}

bool Des::SelfTest() {
    struct Case {
        const char* key;
        const char* plain;
        const char* cipher;
    };
    const Case cases[] = {
        {"133457799BBCDFF1", "0123456789ABCDEF", "85E813540F0AB405"},
        {"0123456789ABCDEF", "4E6F772069732074", "3FA40E8A984D4815"},
        {"0000000000000000", "0000000000000000", "8CA64DE9C1B123A7"},
        {"FFFFFFFFFFFFFFFF", "FFFFFFFFFFFFFFFF", "7359B2163E4EDC58"},
        {"3000000000000000", "1000000000000001", "958E6E627A05557B"},
    };
    for (const Case& c : cases) {
        uint8_t key[8], plain[8], cipher[8], out[8];
        for (int i = 0; i < 8; ++i) {
            auto hexVal = [](char ch) -> int {
                if (ch >= '0' && ch <= '9') return ch - '0';
                if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
                return ch - 'a' + 10;
            };
            key[i] = (uint8_t)(hexVal(c.key[i * 2]) * 16 + hexVal(c.key[i * 2 + 1]));
            plain[i] = (uint8_t)(hexVal(c.plain[i * 2]) * 16 + hexVal(c.plain[i * 2 + 1]));
            cipher[i] = (uint8_t)(hexVal(c.cipher[i * 2]) * 16 + hexVal(c.cipher[i * 2 + 1]));
        }
        uint64_t subkeys[16];
        Des::BuildSubkeys(key, subkeys);
        Des::EncryptBlock(subkeys, plain, out);
        if (memcmp(out, cipher, 8) != 0) return false;
        uint8_t back[8];
        Des::DecryptBlock(subkeys, cipher, back);
        if (memcmp(back, plain, 8) != 0) return false;
    }
    return true;
}

void DesCryptPayloadInPlace(std::vector<uint8_t>& data, const uint8_t key8[8], bool decrypt) {
    const size_t total = data.size();
    if (total == 0) return;
    uint64_t subkeys[16];
    Des::BuildSubkeys(key8, subkeys);

    const size_t full = total / 8 * 8;
    uint8_t prevCipher[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    if (full) {
        std::vector<uint8_t> decrypted;
        if (decrypt) decrypted.resize(full);
        uint8_t cipherBackup[8];
        for (size_t off = 0; off < full; off += 8) {
            if (decrypt) {
                memcpy(cipherBackup, &data[off], 8);
                Des::DecryptBlock(subkeys, &data[off], &decrypted[off]);
                for (int i = 0; i < 8; ++i) data[off + i] = (uint8_t)(decrypted[off + i] ^ prevCipher[i]);
                memcpy(prevCipher, cipherBackup, 8);
            } else {
                uint8_t plainXor[8];
                for (int i = 0; i < 8; ++i) plainXor[i] = (uint8_t)(data[off + i] ^ prevCipher[i]);
                uint8_t cipher[8];
                Des::EncryptBlock(subkeys, plainXor, cipher);
                memcpy(&data[off], cipher, 8);
                memcpy(prevCipher, cipher, 8);
            }
        }
    }

    const size_t r = total - full;
    if (r) {
        uint8_t* t = &data[full];
        if (decrypt) {
            const int parity = (t[r - 1] + t[0]) & 1;
            for (size_t left = 0; left < r / 2; ++left) {
                size_t right = r - 1 - left;
                if (((t[right] + t[left]) & 1) == parity) {
                    uint8_t tmp = t[left];
                    t[left] = t[right];
                    t[right] = tmp;
                }
            }
            uint8_t prior = 0;
            for (size_t i = 0; i < r; ++i) {
                uint8_t v = t[i];
                t[i] = (uint8_t)(v ^ key8[i] ^ prior ^ prevCipher[i]);
                prior = v;
            }
        } else {
            // 与 dpk.py 的 _encrypt 一致：prior 取上一步已变换后的字节（即前一个密文字节）
            uint8_t prior = 0;
            for (size_t i = 0; i < r; ++i) {
                uint8_t v = (uint8_t)(t[i] ^ key8[i] ^ prior ^ prevCipher[i]);
                t[i] = v;
                prior = v;
            }
            const int parity = (t[r - 1] + t[0]) & 1;
            for (size_t left = 0; left < r / 2; ++left) {
                size_t right = r - 1 - left;
                if (((t[right] + t[left]) & 1) == parity) {
                    uint8_t tmp = t[left];
                    t[left] = t[right];
                    t[right] = tmp;
                }
            }
        }
    }
}

std::vector<uint8_t> DesCryptPayload(const std::vector<uint8_t>& in, const uint8_t key8[8], bool decrypt) {
    std::vector<uint8_t> out = in;
    DesCryptPayloadInPlace(out, key8, decrypt);
    return out;
}

}  // namespace dpk