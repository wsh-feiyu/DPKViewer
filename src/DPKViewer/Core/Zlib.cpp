// Zlib.cpp - DEFLATE 解压（puff 风格）与固定 Huffman + LZ77 压缩
#include "Zlib.h"

#include <cstring>

namespace dpk {

namespace {

// ------------------------------------------------------------------ 公共表
const uint16_t LEN_BASE[29] = {3,   4,   5,   6,   7,   8,   9,   10,  11,  13,  15,  17,  19,  23,  27,
                               31,  35,  43,  51,  59,  67,  83,  99,  115, 131, 163, 195, 227, 258};
const uint8_t LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                               2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t DIST_BASE[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,    25,    33,    49,   65,
                                97,   129,  193,  257,  385,  513,   769,   1025,  1537,  2049,  3073,  4097, 6145,
                                8193, 12289, 16385, 24577};
const uint8_t DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

const int CLCL_ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

// ------------------------------------------------------------------ 位读取
class BitReader {
public:
    BitReader(const uint8_t* data, size_t len) : data_(data), len_(len) {}

    // 读取 n 位（LSB 优先）；返回 false 表示越界
    bool GetBits(int n, uint32_t& value) {
        while (bitCount_ < n) {
            if (pos_ >= len_) return false;
            bitBuf_ |= (uint32_t)data_[pos_++] << bitCount_;
            bitCount_ += 8;
        }
        value = bitBuf_ & ((1u << n) - 1);
        bitBuf_ >>= n;
        bitCount_ -= n;
        return true;
    }

    // 读取 1 位，越界返回 -1
    int GetBit() {
        uint32_t v = 0;
        if (!GetBits(1, v)) return -1;
        return (int)v;
    }

    void AlignToByte() {
        bitBuf_ = 0;
        bitCount_ = 0;
    }

    // stored 块用：按字节读取
    bool ReadByte(uint8_t& b) {
        if (bitCount_ >= 8) {
            b = (uint8_t)(bitBuf_ & 0xFF);
            bitBuf_ >>= 8;
            bitCount_ -= 8;
            return true;
        }
        if (pos_ >= len_) return false;
        b = data_[pos_++];
        return true;
    }

    size_t BytesConsumed() const { return pos_; }

private:
    const uint8_t* data_;
    size_t len_;
    size_t pos_ = 0;
    uint32_t bitBuf_ = 0;
    int bitCount_ = 0;
};

// ------------------------------------------------------------------ 位写入
class BitWriter {
public:
    explicit BitWriter(std::vector<uint8_t>& out) : out_(out) {}

    void PutBits(uint32_t value, int n) {
        for (int i = 0; i < n; ++i) {
            bitBuf_ |= ((value >> i) & 1u) << bitCount_;
            if (++bitCount_ == 8) {
                out_.push_back((uint8_t)bitBuf_);
                bitBuf_ = 0;
                bitCount_ = 0;
            }
        }
    }

    // Huffman 码（表中为 MSB 优先）按 LSB 优先写入
    void PutBitsReversed(uint32_t code, int n) {
        for (int i = n - 1; i >= 0; --i) PutBits((code >> i) & 1u, 1);
    }

    void AlignToByte() {
        if (bitCount_ > 0) {
            out_.push_back((uint8_t)bitBuf_);
            bitBuf_ = 0;
            bitCount_ = 0;
        }
    }

    int BitCount() const { return bitCount_; }

private:
    std::vector<uint8_t>& out_;
    uint32_t bitBuf_ = 0;
    int bitCount_ = 0;
};

// ------------------------------------------------------------------ Huffman
struct Huffman {
    uint16_t count[16];
    std::vector<uint16_t> symbol;

    void Reset() {
        memset(count, 0, sizeof(count));
        symbol.clear();
    }

    bool Build(const uint8_t* lengths, int n, const char* what, std::string& err) {
        Reset();
        for (int i = 0; i < n; ++i) {
            if (lengths[i] > 15) {
                err = std::string("Huffman 码长非法(") + what + ")";
                return false;
            }
            count[lengths[i]]++;
        }
        if (count[0] == n) {
            // 全零：允许（空距离码表）
            count[0] = 0;
            return true;
        }
        int left = 1;
        for (int len = 1; len <= 15; ++len) {
            left <<= 1;
            left -= count[len];
            if (left < 0) {
                err = std::string("Huffman 码表过完备(") + what + ")";
                return false;
            }
        }
        uint16_t offs[16];
        offs[1] = 0;
        for (int len = 1; len < 15; ++len) offs[len + 1] = (uint16_t)(offs[len] + count[len]);
        std::vector<uint16_t> sorted((size_t)n);
        for (int i = 0; i < n; ++i) {
            if (lengths[i] != 0) sorted[offs[lengths[i]]++] = (uint16_t)i;
        }
        symbol.assign(sorted.begin(), sorted.begin() + (size_t)n - count[0]);
        return true;
    }

    // 返回符号，-1 表示码表非法，-2 表示数据越界
    int Decode(BitReader& br) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; ++len) {
            int bit = br.GetBit();
            if (bit < 0) return -2;
            code |= bit;
            int cnt = count[len];
            if (code - first < cnt) {
                size_t idx = (size_t)index + (size_t)(code - first);
                if (idx >= symbol.size()) return -1;
                return (int)symbol[idx];
            }
            index += cnt;
            first = (first + cnt) << 1;
            code <<= 1;
        }
        return -1;
    }
};

bool FixedLiteralLengths(uint8_t* lengths) {
    for (int i = 0; i < 288; ++i) {
        if (i < 144) {
            lengths[i] = 8;
        } else if (i < 256) {
            lengths[i] = 9;
        } else if (i < 280) {
            lengths[i] = 7;
        } else {
            lengths[i] = 8;
        }
    }
    return true;
}

int InflateBlockBody(BitReader& br, const Huffman& litLen, const Huffman& dist, std::vector<uint8_t>& out,
                     std::string& err) {
    for (;;) {
        int sym = litLen.Decode(br);
        if (sym == -2) {
            err = "DEFLATE 数据在符号处提前结束";
            return -1;
        }
        if (sym < 0) {
            err = "DEFLATE 非法 Huffman 码";
            return -1;
        }
        if (sym < 256) {
            out.push_back((uint8_t)sym);
            continue;
        }
        if (sym == 256) return 0;
        sym -= 257;
        if (sym >= 29) {
            err = "DEFLATE 长度码越界";
            return -1;
        }
        uint32_t extra = 0;
        if (LEN_EXTRA[sym] && !br.GetBits(LEN_EXTRA[sym], extra)) {
            err = "DEFLATE 长度附加位不足";
            return -1;
        }
        size_t length = (size_t)LEN_BASE[sym] + extra;

        int dsym = dist.Decode(br);
        if (dsym < 0) {
            err = "DEFLATE 非法距离码";
            return -1;
        }
        if (dsym >= 30) {
            err = "DEFLATE 距离码越界";
            return -1;
        }
        uint32_t dextra = 0;
        if (DIST_EXTRA[dsym] && !br.GetBits(DIST_EXTRA[dsym], dextra)) {
            err = "DEFLATE 距离附加位不足";
            return -1;
        }
        size_t distance = (size_t)DIST_BASE[dsym] + dextra;
        if (distance == 0 || distance > out.size()) {
            err = "DEFLATE 距离超出已输出数据";
            return -1;
        }
        size_t start = out.size() - distance;
        for (size_t i = 0; i < length; ++i) out.push_back(out[start + i]);
    }
}

bool InflateRaw(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, std::string& err) {
    BitReader br(src, srcLen);
    Huffman fixedLit, fixedDist;
    bool fixedReady = false;

    for (;;) {
        uint32_t last = 0, type = 0;
        if (!br.GetBits(1, last) || !br.GetBits(2, type)) {
            err = "DEFLATE 块头不完整";
            return false;
        }
        if (type == 0) {
            br.AlignToByte();
            uint8_t l0, l1, n0, n1;
            if (!br.ReadByte(l0) || !br.ReadByte(l1) || !br.ReadByte(n0) || !br.ReadByte(n1)) {
                err = "DEFLATE stored 块头不完整";
                return false;
            }
            size_t len = (size_t)l0 | ((size_t)l1 << 8);
            size_t nlen = (size_t)n0 | ((size_t)n1 << 8);
            if ((len ^ 0xFFFF) != nlen) {
                err = "DEFLATE stored 块长度校验失败";
                return false;
            }
            for (size_t i = 0; i < len; ++i) {
                uint8_t b;
                if (!br.ReadByte(b)) {
                    err = "DEFLATE stored 块数据不足";
                    return false;
                }
                out.push_back(b);
            }
        } else if (type == 1 || type == 2) {
            Huffman dynLit, dynDist;
            const Huffman* lit = &fixedLit;
            const Huffman* dist = &fixedDist;
            if (type == 1) {
                if (!fixedReady) {
                    uint8_t litLengths[288];
                    uint8_t distLengths[30];
                    FixedLiteralLengths(litLengths);
                    for (int i = 0; i < 30; ++i) distLengths[i] = 5;
                    if (!fixedLit.Build(litLengths, 288, "固定字面码表", err) ||
                        !fixedDist.Build(distLengths, 30, "固定距离码表", err)) {
                        return false;
                    }
                    fixedReady = true;
                }
            } else {
                uint32_t hlit = 0, hdist = 0, hclen = 0;
                if (!br.GetBits(5, hlit) || !br.GetBits(5, hdist) || !br.GetBits(4, hclen)) {
                    err = "DEFLATE 动态块头不完整";
                    return false;
                }
                int nlit = (int)hlit + 257;
                int ndist = (int)hdist + 1;
                int nclen = (int)hclen + 4;
                if (nlit > 286 || ndist > 30) {
                    err = "DEFLATE 动态块码表规模非法";
                    return false;
                }
                uint8_t clLengths[19];
                memset(clLengths, 0, sizeof(clLengths));
                for (int i = 0; i < nclen; ++i) {
                    uint32_t v = 0;
                    if (!br.GetBits(3, v)) {
                        err = "DEFLATE 码长码表不完整";
                        return false;
                    }
                    clLengths[CLCL_ORDER[i]] = (uint8_t)v;
                }
                Huffman clHuff;
                if (!clHuff.Build(clLengths, 19, "码长码表", err)) return false;

                uint8_t lengths[320];
                memset(lengths, 0, sizeof(lengths));
                int index = 0;
                while (index < nlit + ndist) {
                    int sym = clHuff.Decode(br);
                    if (sym == -2) {
                        err = "DEFLATE 码长数据提前结束";
                        return false;
                    }
                    if (sym < 0) {
                        err = "DEFLATE 非法码长码";
                        return false;
                    }
                    if (sym < 16) {
                        lengths[index++] = (uint8_t)sym;
                    } else if (sym == 16) {
                        if (index == 0) {
                            err = "DEFLATE 码长重复无前值";
                            return false;
                        }
                        uint32_t rep = 0;
                        if (!br.GetBits(2, rep)) {
                            err = "DEFLATE 码长重复位不足";
                            return false;
                        }
                        uint8_t prev = lengths[index - 1];
                        for (int i = 0; i < 3 + (int)rep && index < nlit + ndist; ++i) lengths[index++] = prev;
                    } else if (sym == 17 || sym == 18) {
                        uint32_t rep = 0;
                        int nbits = (sym == 17) ? 3 : 7;
                        int base = (sym == 17) ? 3 : 11;
                        if (!br.GetBits(nbits, rep)) {
                            err = "DEFLATE 零重复位不足";
                            return false;
                        }
                        int total = base + (int)rep;
                        for (int i = 0; i < total && index < nlit + ndist; ++i) lengths[index++] = 0;
                    } else {
                        err = "DEFLATE 非法码长符号";
                        return false;
                    }
                }
                if (lengths[256] == 0) {
                    err = "DEFLATE 动态块缺少结束码";
                    return false;
                }
                if (!dynLit.Build(lengths, nlit, "动态字面码表", err)) return false;
                if (!dynDist.Build(lengths + nlit, ndist, "动态距离码表", err)) return false;
                lit = &dynLit;
                dist = &dynDist;
            }
            if (InflateBlockBody(br, *lit, *dist, out, err) != 0) return false;
        } else {
            err = "DEFLATE 保留块类型";
            return false;
        }
        if (last) break;
    }
    return true;
}

}  // namespace

uint32_t Adler32(const uint8_t* data, size_t len, uint32_t seed) {
    uint32_t a = seed & 0xFFFF, b = (seed >> 16) & 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

bool ZlibDecompress(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, std::string& err) {
    out.clear();
    if (srcLen < 2) {
        err = "zlib 流长度不足";
        return false;
    }
    uint8_t cmf = src[0], flg = src[1];
    if ((cmf & 0x0F) != 8) {
        err = "zlib 压缩方法非 deflate";
        return false;
    }
    if ((((uint32_t)cmf << 8) | flg) % 31 != 0) {
        err = "zlib 头校验失败";
        return false;
    }
    if (flg & 0x20) {
        err = "不支持 zlib 预置字典";
        return false;
    }
    size_t bodyLen = srcLen - 2;
    if (bodyLen < 4) {
        err = "zlib 流长度不足（缺少 Adler-32）";
        return false;
    }
    bodyLen -= 4;  // 末尾 4 字节为 Adler-32
    if (!InflateRaw(src + 2, bodyLen, out, err)) {
        out.clear();
        return false;
    }
    const uint8_t* tail = src + srcLen - 4;
    uint32_t expect = ((uint32_t)tail[0] << 24) | ((uint32_t)tail[1] << 16) | ((uint32_t)tail[2] << 8) | tail[3];
    uint32_t actual = Adler32(out.empty() ? nullptr : &out[0], out.size(), 1);
    if (expect != actual) {
        err = "zlib Adler-32 校验失败";
        out.clear();
        return false;
    }
    return true;
}

namespace {

void EmitFixedLiteral(BitWriter& bw, int sym) {
    if (sym < 144) {
        bw.PutBitsReversed((uint32_t)(0x30 + sym), 8);
    } else if (sym < 256) {
        bw.PutBitsReversed((uint32_t)(0x190 + (sym - 144)), 9);
    } else if (sym < 280) {
        bw.PutBitsReversed((uint32_t)(sym - 256), 7);
    } else {
        bw.PutBitsReversed((uint32_t)(0xC0 + (sym - 280)), 8);
    }
}

void LenCode(size_t length, int& sym, uint32_t& extraVal, int& extraBits) {
    for (int i = 0; i < 29; ++i) {
        size_t base = LEN_BASE[i];
        size_t span = (size_t)1 << LEN_EXTRA[i];
        if (length >= base && length < base + span) {
            sym = 257 + i;
            extraVal = (uint32_t)(length - base);
            extraBits = LEN_EXTRA[i];
            return;
        }
    }
    sym = 285;
    extraVal = 0;
    extraBits = 0;
}

void DistCode(size_t distance, int& sym, uint32_t& extraVal, int& extraBits) {
    for (int i = 0; i < 30; ++i) {
        size_t base = DIST_BASE[i];
        size_t span = (size_t)1 << DIST_EXTRA[i];
        if (distance >= base && distance < base + span) {
            sym = i;
            extraVal = (uint32_t)(distance - base);
            extraBits = DIST_EXTRA[i];
            return;
        }
    }
    sym = 29;
    extraVal = 0;
    extraBits = 0;
}

// 固定 Huffman + 贪心 LZ77
bool DeflateFixed(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out) {
    const int HASH_BITS = 15;
    const size_t HASH_SIZE = (size_t)1 << HASH_BITS;
    const size_t MAX_MATCH = 258;
    const size_t WINDOW = 32768;
    const int MAX_CHAIN = 64;

    out.push_back(0x78);
    out.push_back(0x9C);
    BitWriter bw(out);
    bw.PutBits(1, 1);  // BFINAL
    bw.PutBits(1, 2);  // BTYPE = 01 固定 Huffman

    std::vector<int32_t> head(HASH_SIZE, -1);
    std::vector<int32_t> prev(srcLen ? srcLen : 1, -1);

    auto Hash3 = [&](size_t p) -> size_t {
        uint32_t v = ((uint32_t)src[p] << 16) | ((uint32_t)src[p + 1] << 8) | (uint32_t)src[p + 2];
        return (size_t)((v * 2654435761u) >> (32 - HASH_BITS));
    };
    auto Insert = [&](size_t p) {
        if (p + 3 > srcLen) return;
        size_t h = Hash3(p);
        prev[p] = head[h];
        head[h] = (int32_t)p;
    };

    size_t pos = 0;
    while (pos < srcLen) {
        size_t bestLen = 0, bestDist = 0;
        if (pos + 3 <= srcLen) {
            size_t h = Hash3(pos);
            int32_t cand = head[h];
            int chain = 0;
            while (cand >= 0 && chain < MAX_CHAIN) {
                size_t c = (size_t)cand;
                if (c >= pos) break;
                size_t dist = pos - c;
                if (dist > WINDOW) break;
                size_t maxLen = srcLen - pos;
                if (maxLen > MAX_MATCH) maxLen = MAX_MATCH;
                size_t len = 0;
                while (len < maxLen && src[c + len] == src[pos + len]) ++len;
                if (len > bestLen) {
                    bestLen = len;
                    bestDist = dist;
                    if (len == maxLen) break;
                }
                cand = prev[c];
                ++chain;
            }
        }
        if (bestLen >= 3) {
            int lsym, lbits;
            uint32_t lval;
            LenCode(bestLen, lsym, lval, lbits);
            EmitFixedLiteral(bw, lsym);
            if (lbits) bw.PutBits(lval, lbits);
            int dsym, dbits;
            uint32_t dval;
            DistCode(bestDist, dsym, dval, dbits);
            bw.PutBitsReversed((uint32_t)dsym, 5);
            if (dbits) bw.PutBits(dval, dbits);
            for (size_t i = 0; i < bestLen; ++i) Insert(pos + i);
            pos += bestLen;
        } else {
            EmitFixedLiteral(bw, src[pos]);
            Insert(pos);
            ++pos;
        }
    }
    EmitFixedLiteral(bw, 256);  // 块结束
    bw.AlignToByte();

    uint32_t adler = Adler32(src, srcLen, 1);
    out.push_back((uint8_t)(adler >> 24));
    out.push_back((uint8_t)(adler >> 16));
    out.push_back((uint8_t)(adler >> 8));
    out.push_back((uint8_t)adler);
    return true;
}

void DeflateStored(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out) {
    out.push_back(0x78);
    out.push_back(0x01);
    size_t pos = 0;
    do {
        size_t chunk = srcLen - pos;
        if (chunk > 65535) chunk = 65535;
        bool last = (pos + chunk >= srcLen);
        out.push_back((uint8_t)(last ? 1 : 0));  // BFINAL + BTYPE=00，恰好字节对齐
        out.push_back((uint8_t)(chunk & 0xFF));
        out.push_back((uint8_t)((chunk >> 8) & 0xFF));
        uint16_t nlen = (uint16_t)(~chunk);
        out.push_back((uint8_t)(nlen & 0xFF));
        out.push_back((uint8_t)((nlen >> 8) & 0xFF));
        if (chunk) out.insert(out.end(), src + pos, src + pos + chunk);
        pos += chunk;
    } while (pos < srcLen);
    uint32_t adler = Adler32(src, srcLen, 1);
    out.push_back((uint8_t)(adler >> 24));
    out.push_back((uint8_t)(adler >> 16));
    out.push_back((uint8_t)(adler >> 8));
    out.push_back((uint8_t)adler);
}

}  // namespace

bool ZlibCompress(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, int level, std::string& err) {
    out.clear();
    if (level <= 0) {
        DeflateStored(src, srcLen, out);
        return true;
    }
    std::vector<uint8_t> packed;
    DeflateFixed(src, srcLen, packed);
    std::vector<uint8_t> stored;
    DeflateStored(src, srcLen, stored);
    // 压缩结果不优于 stored 时使用 stored，保证输出不会膨胀
    out = (packed.size() <= stored.size()) ? packed : stored;
    (void)err;
    return true;
}

bool ZlibSelfTest(std::string& detail) {
    std::vector<std::vector<uint8_t>> samples;
    samples.push_back(std::vector<uint8_t>());
    samples.push_back(std::vector<uint8_t>(1, 0x41));
    samples.push_back(std::vector<uint8_t>{0x68, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x44, 0x50, 0x4B});
    {
        std::vector<uint8_t> repeated;
        for (int i = 0; i < 5000; ++i) repeated.push_back((uint8_t)("abcabcabc"[i % 9]));
        samples.push_back(repeated);
    }
    {
        std::vector<uint8_t> text;
        const char* line = "whpackage1.0 目录树 / 路径名表 测试数据 0123456789\r\n";
        for (int i = 0; i < 400; ++i) {
            const char* p = line;
            while (*p) text.push_back((uint8_t)*p++);
        }
        samples.push_back(text);
    }
    {
        std::vector<uint8_t> randomish;
        uint32_t seed = 12345;
        for (int i = 0; i < 20000; ++i) {
            seed = seed * 1103515245u + 12345u;
            randomish.push_back((uint8_t)(seed >> 16));
        }
        samples.push_back(randomish);
    }
    {
        // 覆盖 deflate 各编码路径容易出错的长度区间（含 stored/固定 Huffman 边界）
        for (size_t n = 0; n <= 8; ++n) samples.push_back(std::vector<uint8_t>(n, 0x5A));
        for (size_t n = 250; n <= 262; ++n) samples.push_back(std::vector<uint8_t>(n, 0x5A));
        for (size_t n : {(size_t)1015, (size_t)1016, (size_t)1017, (size_t)65535, (size_t)65536}) {
            samples.push_back(std::vector<uint8_t>(n, 0x7E));
        }
    }

    for (size_t i = 0; i < samples.size(); ++i) {
        for (int level : {0, 1, 6, 9}) {
            std::vector<uint8_t> packed, back;
            std::string err;
            if (!ZlibCompress(samples[i].empty() ? nullptr : &samples[i][0], samples[i].size(), packed, level, err)) {
                detail = "压缩失败: " + err;
                return false;
            }
            if (!ZlibDecompress(packed.empty() ? nullptr : &packed[0], packed.size(), back, err)) {
                detail = "解压失败: " + err;
                return false;
            }
            if (back != samples[i]) {
                detail = "样本 " + std::to_string(i) + " level " + std::to_string(level) + " 往返不一致";
                return false;
            }
        }
    }
    detail = "ok";
    return true;
}

}  // namespace dpk