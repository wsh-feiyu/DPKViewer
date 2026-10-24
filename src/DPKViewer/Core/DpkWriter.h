// DpkWriter.h - DPK 包生成（打包）
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "DpkTypes.h"

namespace dpk {

// 打包选项
struct PackOptions {
    bool compress = true;  // 压缩（flags & 0x01）
    bool encrypt = true;   // 加密（flags & 0x02，使用 PackagePassword）
    int level = 9;         // 压缩级别，<=0 时使用 stored 块
    std::vector<uint8_t> templateHeader;  // 可选：1024 字节模板包头（沿用原包头字段）
};

// 打包结果统计
struct PackResult {
    uint32_t entryCount = 0;    // 写入条目的数量
    uint32_t skippedCount = 0;  // 因路径非法被跳过的数量
    uint64_t plainBytes = 0;    // 明文总量
    uint64_t packageSize = 0;   // 生成的包大小（字节）
    uint32_t totalBlocks = 0;   // 总块数
};

// 待打包的条目
struct PackFile {
    std::wstring relPath;         // 包内相对路径（'/' 或 '\\' 均可）
    std::vector<uint8_t> data;    // 文件内容
};

// 从内存条目打包
bool PackFiles(const std::vector<PackFile>& files, const std::wstring& outPath, const PackOptions& opt,
               PackResult& result, std::string& err, const ProgressCallback& progress = ProgressCallback());

// 从目录递归打包
bool PackFolder(const std::wstring& srcDir, const std::wstring& outPath, const PackOptions& opt, PackResult& result,
                std::string& err, const ProgressCallback& progress = ProgressCallback());

// 生成只含空目录树/路径表的包
bool PackEmpty(const std::wstring& outPath, const PackOptions& opt, PackResult& result, std::string& err);

// 读取模板包头（1024 字节）
bool LoadTemplateHeader(const std::wstring& templateDpkPath, std::vector<uint8_t>& header, std::string& err);

}  // namespace dpk