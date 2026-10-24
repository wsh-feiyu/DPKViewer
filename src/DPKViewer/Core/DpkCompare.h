// DpkCompare.h - 两个 DPK 包的差异对比（不依赖 MFC，GUI 与 CLI 共用）
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "DpkTypes.h"
#include "Whsc.h"

namespace dpk {

// 对比深度
enum class CompareDepth {
    PathsOnly,  // 仅比对条目清单（增/删），不读任何条目内容
    Metadata,   // 路径 + 流头字段（flags/明文/存储）+ 流头内嵌 MD5 摘要，每条目只读根块 1KB
    Full,       // 逐条目完整解码并逐字节比对（同时校验两侧 MD5，可检出包内损坏）
};

enum class CompareStatus { Same, Different, OnlyA, OnlyB, Error };

// 条目在某一侧的信息
struct CompareSideInfo {
    bool present = false;    // 该侧是否存在此条目
    uint32_t rootBlock = 0;
    bool headerOk = false;   // PeekRootBlock + WhscPeekHeader 是否成功
    WhscInfo header;         // flags / plainSize / storedSize 等
    uint8_t md5[16] = {0};   // 流头内嵌的明文 MD5 摘要（headerOk 时有效）
    bool decoded = false;    // Full 深度下解码成功（隐含 MD5 校验通过）
    std::string err;         // 该侧读取/解码失败原因（UTF-8）
};

// 一条对比结果
struct CompareEntry {
    std::string pathUtf8;
    CompareStatus status = CompareStatus::Same;
    std::string note;        // 差异说明（如 "MD5 摘要不同" / "内容一致，存储参数不同"）
    bool contentSame = false;  // 内容已验证一致（状态可为 Different：仅打包参数不同）
    CompareSideInfo a, b;
};

// 包级信息
struct ComparePackageInfo {
    bool opened = false;
    std::string err;
    uint64_t fileSize = 0;
    uint32_t totalBlocks = 0;
    uint32_t treeRoot = 0;
    uint32_t namesRoot = 0;
    size_t entryCount = 0;
};

struct CompareStats {
    size_t same = 0;
    size_t different = 0;
    size_t onlyA = 0;
    size_t onlyB = 0;
    size_t errors = 0;
    size_t Total() const { return same + different + onlyA + onlyB + errors; }
};

struct CompareResult {
    ComparePackageInfo pkgA, pkgB;
    std::vector<CompareEntry> entries;  // 按路径字节序升序
    CompareStats stats;
    bool completed = false;             // false = 用户取消（entries 为部分结果）
};

// 对比两个包。失败（任一包打不开/目录解析失败）返回 false 并填 err；
// 用户取消返回 true 但 result.completed=false。
bool ComparePackages(const std::wstring& pathA, const std::wstring& pathB, CompareDepth depth,
                     CompareResult& result, std::string& err, const ProgressCallback& progress = ProgressCallback());

// 生成 UTF-8 文本报告（摘要 + 包信息 + 全部非「相同」条目明细）
std::string CompareReportText(const CompareResult& result, CompareDepth depth);

}  // namespace dpk
