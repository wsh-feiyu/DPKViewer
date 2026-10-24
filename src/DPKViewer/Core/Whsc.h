// Whsc.h - whsc1.0 虚拟文件流编解码
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dpk {

extern const char kPackagePassword[];  // "tianxiawudi"
extern const char kContentPassword[];  // "gandiaosifu"
extern const size_t kBlockSize;        // 1024

// whsc 流头信息
struct WhscInfo {
    uint8_t flags = 0;
    uint32_t plainSize = 0;
    uint32_t storedSize = 0;
    bool md5Ok = false;
    bool compressed = false;
    bool encrypted = false;
    bool contentPassword = false;
};

// 仅解析 48 字节流头
bool WhscPeekHeader(const std::vector<uint8_t>& stream, WhscInfo& info, std::string& err);

// whsc 流 -> 明文（校验长度与 MD5）
bool WhscDecode(const std::vector<uint8_t>& stream, std::vector<uint8_t>& plain, WhscInfo& info, std::string& err);

// 明文 -> whsc 流
//   flags: 0x01 压缩，0x02 加密，0x04 使用 ContentPassword（否则 PackagePassword）
//   level: 压缩级别，<=0 时使用 stored 块（不压缩数据只加 zlib 包装）
//   adaptive: 压缩仅在结果更小时使用——压缩后不小于原文就按原样存储并清掉 0x01 位，
//             与游戏原始打包器的逐条目策略一致（小文件/已压缩数据不压）
bool WhscEncode(const std::vector<uint8_t>& plain, uint8_t flags, std::vector<uint8_t>& stream, std::string& err,
                int level = 9, bool adaptive = false);

// 虚拟文件所需块数（与参考实现 blocks_for 一致）
uint32_t BlocksFor(uint64_t size);

}  // namespace dpk