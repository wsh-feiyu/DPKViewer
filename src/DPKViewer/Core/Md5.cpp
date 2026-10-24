// Md5.cpp - RFC 1321 实现
#include "Md5.h"

#include <cstring>

namespace dpk {

namespace {

inline uint32_t Rotl(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

const uint32_t K[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau, 0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u, 0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u};

const int S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                   5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                   4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                   6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

void ProcessBlock(const uint8_t* block, uint32_t h[4]) {
    uint32_t m[16];
    for (int i = 0; i < 16; ++i) {
        m[i] = (uint32_t)block[i * 4] | ((uint32_t)block[i * 4 + 1] << 8) | ((uint32_t)block[i * 4 + 2] << 16) |
               ((uint32_t)block[i * 4 + 3] << 24);
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 64; ++i) {
        uint32_t f;
        int g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        uint32_t tmp = d;
        d = c;
        c = b;
        b = b + Rotl(a + f + K[i] + m[g], S[i]);
        a = tmp;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
}

}  // namespace

Md5Digest Md5Compute(const uint8_t* data, size_t len) {
    uint32_t h[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    size_t full = len / 64;
    for (size_t i = 0; i < full; ++i) ProcessBlock(data + i * 64, h);

    uint8_t tail[128];
    size_t rest = len - full * 64;
    memset(tail, 0, sizeof(tail));
    if (rest) memcpy(tail, data + full * 64, rest);
    tail[rest] = 0x80;
    size_t tailLen = (rest + 1 <= 56) ? 64 : 128;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; ++i) tail[tailLen - 8 + i] = (uint8_t)((bits >> (8 * i)) & 0xFF);
    for (size_t off = 0; off < tailLen; off += 64) ProcessBlock(tail + off, h);

    Md5Digest out;
    for (int i = 0; i < 4; ++i) {
        out[i * 4 + 0] = (uint8_t)(h[i] & 0xFF);
        out[i * 4 + 1] = (uint8_t)((h[i] >> 8) & 0xFF);
        out[i * 4 + 2] = (uint8_t)((h[i] >> 16) & 0xFF);
        out[i * 4 + 3] = (uint8_t)((h[i] >> 24) & 0xFF);
    }
    return out;
}

Md5Digest Md5Compute(const std::vector<uint8_t>& data) {
    return data.empty() ? Md5Compute(static_cast<const uint8_t*>(nullptr), 0) : Md5Compute(&data[0], data.size());
}

Md5Digest Md5Compute(const std::string& text) {
    return text.empty() ? Md5Compute(static_cast<const uint8_t*>(nullptr), 0)
                        : Md5Compute(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

bool Md5Equals(const Md5Digest& a, const Md5Digest& b) { return memcmp(a.data(), b.data(), 16) == 0; }

std::string Md5ToHex(const Md5Digest& d) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (int i = 0; i < 16; ++i) {
        out.push_back(hex[(d[i] >> 4) & 0xF]);
        out.push_back(hex[d[i] & 0xF]);
    }
    return out;
}

bool Md5SelfTest() {
    struct Case {
        const char* text;
        const char* hex;
    };
    const Case cases[] = {
        {"", "d41d8cd98f00b204e9800998ecf8427e"},
        {"a", "0cc175b9c0f1b6a831c399e269772661"},
        {"abc", "900150983cd24fb0d6963f7d28e17f72"},
        {"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
        {"abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b"},
        {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "d174ab98d277d9f5a5611c2c9f419d9f"},
        {"12345678901234567890123456789012345678901234567890123456789012345678901234567890",
         "57edf4a22be3c955ac49da2e2107b67a"},
    };
    for (const Case& c : cases) {
        if (Md5ToHex(Md5Compute(std::string(c.text))) != std::string(c.hex)) return false;
    }
    return true;
}

}  // namespace dpk