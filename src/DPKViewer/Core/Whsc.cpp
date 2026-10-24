// Whsc.cpp - whsc1.0 流编解码（与 dpk.py 行为一致）
#include "Whsc.h"

#include <cstring>

#include "Des.h"
#include "Md5.h"
#include "Zlib.h"

namespace dpk {

const char kPackagePassword[] = "tianxiawudi";
const char kContentPassword[] = "gandiaosifu";
const size_t kBlockSize = 1024;

namespace {

const uint32_t kWhscTag = 0x543B3338u;

uint32_t ReadU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void WriteU32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

}  // namespace

uint32_t BlocksFor(uint64_t size) {
    const uint64_t firstCapacity = kBlockSize - 32;  // 992
    if (size <= firstCapacity) return 1;
    const uint64_t rest = kBlockSize - 8;  // 1016
    return (uint32_t)(1 + (size - firstCapacity + rest - 1) / rest);
}

bool WhscPeekHeader(const std::vector<uint8_t>& stream, WhscInfo& info, std::string& err) {
    if (stream.size() < 48) {
        err = "whsc 流长度不足 48 字节";
        return false;
    }
    if (memcmp(&stream[0], "whsc1.0", 7) != 0) {
        err = "whsc 魔数不匹配（不是 whsc1.0 流）";
        return false;
    }
    info.flags = stream[15];
    info.storedSize = ReadU32(&stream[40]);
    info.plainSize = ReadU32(&stream[36]);
    info.compressed = (info.flags & 0x01) != 0;
    info.encrypted = (info.flags & 0x02) != 0;
    info.contentPassword = (info.flags & 0x04) != 0;
    // 注意：这里不再校验 48 + storedSize <= stream.size()
    // PeekHeader 职责仅为解析头字段，负载长度完整性由 WhscDecode 负责
    return true;
}

bool WhscDecode(const std::vector<uint8_t>& stream, std::vector<uint8_t>& plain, WhscInfo& info, std::string& err) {
    plain.clear();
    if (!WhscPeekHeader(stream, info, err)) return false;

    std::vector<uint8_t> payload(stream.begin() + 48, stream.begin() + 48 + info.storedSize);

    if (info.encrypted) {
        const char* pwd = info.contentPassword ? kContentPassword : kPackagePassword;
        uint8_t key8[8];
        memset(key8, 0, sizeof(key8));
        memcpy(key8, pwd, strlen(pwd) < 8 ? strlen(pwd) : 8);
        payload = DesCryptPayload(payload, key8, true);
    }
    if (info.compressed) {
        std::vector<uint8_t> inflated;
        std::string zerr;
        if (!ZlibDecompress(payload.empty() ? nullptr : &payload[0], payload.size(), inflated, zerr)) {
            err = "zlib 解压失败: " + zerr;
            return false;
        }
        payload.swap(inflated);
    }
    if (payload.size() != info.plainSize) {
        err = "whsc 明文长度不符 (" + std::to_string(payload.size()) + " != " + std::to_string(info.plainSize) + ")";
        return false;
    }
    Md5Digest digest = payload.empty() ? Md5Compute(static_cast<const uint8_t*>(nullptr), 0)
                                       : Md5Compute(&payload[0], payload.size());
    Md5Digest expect;
    memcpy(expect.data(), &stream[16], 16);
    info.md5Ok = Md5Equals(digest, expect);
    if (!info.md5Ok) {
        err = "MD5 校验失败（内容与原包记录不一致）";
        return false;
    }
    plain.swap(payload);
    return true;
}

bool WhscEncode(const std::vector<uint8_t>& plain, uint8_t flags, std::vector<uint8_t>& stream, std::string& err,
                int level, bool adaptive) {
    stream.clear();
    bool compress = (flags & 0x01) != 0;
    const bool encrypt = (flags & 0x02) != 0;

    std::vector<uint8_t> payload = plain;
    if (compress) {
        std::vector<uint8_t> packed;
        if (!ZlibCompress(payload.empty() ? nullptr : &payload[0], payload.size(), packed, level, err)) return false;
        if (adaptive && packed.size() >= payload.size()) {
            compress = false;  // 压缩不缩反涨：按原样存储
        } else {
            payload.swap(packed);
        }
    }
    if (encrypt) {
        const char* pwd = (flags & 0x04) ? kContentPassword : kPackagePassword;
        uint8_t key8[8];
        memset(key8, 0, sizeof(key8));
        memcpy(key8, pwd, strlen(pwd) < 8 ? strlen(pwd) : 8);
        payload = DesCryptPayload(payload, key8, false);
    }

    const uint8_t actualFlags =
        (uint8_t)((compress ? 0x01 : 0x00) | (encrypt ? 0x02 : 0x00) | (flags & 0x04));
    stream.assign(48, 0);
    memcpy(&stream[0], "whsc1.0", 7);
    stream[15] = actualFlags;
    Md5Digest digest = plain.empty() ? Md5Compute(static_cast<const uint8_t*>(nullptr), 0)
                                     : Md5Compute(&plain[0], plain.size());
    memcpy(&stream[16], digest.data(), 16);
    WriteU32(&stream[36], (uint32_t)plain.size());
    WriteU32(&stream[40], (uint32_t)payload.size());
    WriteU32(&stream[44], kWhscTag);
    stream.insert(stream.end(), payload.begin(), payload.end());
    return true;
}

}  // namespace dpk