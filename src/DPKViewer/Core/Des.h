// Des.h - 自包含 DES 实现 + DPK 使用的 DES 变体（CBC 零 IV + 尾块混淆）
#pragma once

#include <cstdint>
#include <vector>

namespace dpk {

// 标准 DES：16 个子密钥（48 位，必须用 64 位容器保存），仅处理 8 字节块
class Des {
public:
    static void BuildSubkeys(const uint8_t key8[8], uint64_t subkeys[16]);
    static void EncryptBlock(const uint64_t subkeys[16], const uint8_t in[8], uint8_t out[8]);
    static void DecryptBlock(const uint64_t subkeys[16], const uint8_t in[8], uint8_t out[8]);
    // 标准测试向量自检（5 组公开向量，加密与解密双向）
    static bool SelfTest();
};

// DPK 的 DES 变体：
//   完整块：DES-CBC，IV 全 0
//   尾块（1..7 字节）：交换 + 链式异或（见 docs/DPK_FORMAT.md §5.2）
// 输入输出长度一致；密钥为口令前 8 字节。
std::vector<uint8_t> DesCryptPayload(const std::vector<uint8_t>& in, const uint8_t key8[8], bool decrypt);
void DesCryptPayloadInPlace(std::vector<uint8_t>& data, const uint8_t key8[8], bool decrypt);

}  // namespace dpk