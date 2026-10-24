// Md5.h - 自包含 MD5 实现（无外部依赖）
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dpk {

typedef std::array<uint8_t, 16> Md5Digest;

Md5Digest Md5Compute(const uint8_t* data, size_t len);
Md5Digest Md5Compute(const std::vector<uint8_t>& data);
Md5Digest Md5Compute(const std::string& text);
bool Md5Equals(const Md5Digest& a, const Md5Digest& b);
std::string Md5ToHex(const Md5Digest& d);
bool Md5SelfTest();

}  // namespace dpk