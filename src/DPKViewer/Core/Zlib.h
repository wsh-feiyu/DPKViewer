// Zlib.h - 自包含 zlib（DEFLATE 压缩 / 解压 + Adler-32），无外部依赖
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dpk {

uint32_t Adler32(const uint8_t* data, size_t len, uint32_t seed = 1);

// zlib 格式解压（校验收尾 Adler-32，与 Python zlib.decompress 行为一致）
bool ZlibDecompress(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, std::string& err);

// zlib 格式压缩：固定 Huffman + 贪心 LZ77；level <= 0 时退化为 stored 块。
// 输出始终是能被标准 zlib 解压的合法流。
bool ZlibCompress(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, int level, std::string& err);

// 内部自检：压缩后再解压应与原文一致（覆盖多种数据形态）
bool ZlibSelfTest(std::string& detail);

}  // namespace dpk