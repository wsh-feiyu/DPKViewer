// DpkWriter.h - DPK 包生成（打包）
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "DpkTypes.h"

namespace dpk {

// 打包选项
struct PackLayoutEntry {
    uint32_t rootBlock = 0;  // 原包中该条目的根块号
    uint32_t blocks = 0;     // 原包中该条目的块链长度
    std::vector<uint32_t> chain;  // 原包真实的块号序列（块链可能不连续）
};

// 模板布局：按原包块号回填，保证总块数/块号与模板一致
struct PackLayout {
    std::map<std::string, PackLayoutEntry> byPath;  // 小写 UTF-8 路径 -> 原块位置
    std::vector<std::string> entryOrder;            // 原包目录树中的条目顺序（小写 UTF-8 路径）
    uint32_t totalBlocks = 0;                       // 模板总块数
    uint32_t treeRoot = 1;
    uint32_t namesRoot = 2;
    std::vector<uint32_t> treeBlocks;   // treeRoot 整条链的块号（含根块）
    std::vector<uint32_t> namesBlocks;  // namesRoot 整条链的块号（含根块）
    std::vector<uint32_t> freeBlocks;   // 原包中未被任何链引用的空闲块（升序）
    std::wstring templatePath;          // 模板包路径（用于拷贝未被覆盖的块）
};

struct PackOptions {
    bool compress = true;  // 压缩（flags & 0x01）
    bool encrypt = true;   // 加密（flags & 0x02，使用 PackagePassword）
    int level = 9;         // 压缩级别，<=0 时使用 stored 块
    std::vector<uint8_t> templateHeader;  // 可选：1024 字节模板包头（沿用原包头字段）
    std::shared_ptr<PackLayout> layout;   // 可选：按模板布局回填（最贴近原包结构）
};

// 打包结果统计
struct PackResult {
    uint32_t entryCount = 0;    // 写入条目的数量
    uint32_t skippedCount = 0;  // 因路径非法被跳过的数量
    uint32_t reusedCount = 0;   // 模板模式下内容未变、原样复用原包字节的条目数
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

// 读取模板布局（各条目原块位置 + tree/names 链 + 总块数），用于按原包结构回填。
// 大包逐块扫描较慢，progress 会按 0..100 报告扫描进度（返回 false 取消）。
bool LoadPackLayout(const std::wstring& templateDpkPath, PackLayout& layout, std::string& err,
                    const ProgressCallback& progress = ProgressCallback());

// 原位补丁：把 data 写入包内 pathUtf8（同名替换 / 不存在则新增），其余条目字节不动。
// 大包友好：不缓存整包内容、不解码无关条目，内存占用 ≈ 导入文件大小 + 目录树。
// replaced 返回是否为替换；entryCount/totalBlocks 返回补丁后的条目数与总块数。
bool PatchEntryInPackage(const std::wstring& pkgPath, const std::string& pathUtf8,
                         const std::vector<uint8_t>& data, bool compress, bool encrypt, int level,
                         uint32_t& entryCount, uint32_t& totalBlocksOut, bool& replaced, std::string& err,
                         const ProgressCallback& progress = ProgressCallback());

}  // namespace dpk