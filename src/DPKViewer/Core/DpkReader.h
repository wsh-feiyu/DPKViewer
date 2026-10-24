// DpkReader.h - DPK 包读取：浏览、条目解码、解包导出
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "DpkTypes.h"
#include "Whsc.h"

namespace dpk {

class Reader {
public:
    Reader();
    ~Reader();

    bool Open(const std::wstring& path, std::string& err);
    void Close();
    bool IsOpen() const { return fp_ != nullptr; }

    const std::wstring& Path() const { return path_; }
    uint64_t FileSize() const { return size_; }
    uint32_t TreeRoot() const { return treeRoot_; }
    uint32_t NamesRoot() const { return namesRoot_; }
    uint32_t TotalBlocks() const { return (uint32_t)(size_ / kBlockSize); }

    // 解析目录树与路径名表（computeBlockCounts 为 true 时同时统计各条目块数）
    bool LoadEntries(std::string& err, bool computeBlockCounts = true);
    bool EntriesLoaded() const { return entriesLoaded_; }
    const std::vector<Entry>& Entries() const { return entries_; }

    // 读取块链（虚拟文件原始字节）
    bool ReadVirtualFile(uint32_t rootBlock, std::vector<uint8_t>& out, std::string& err) const;
    // 只读取根块前 1KB，跳过 32 字节块头后把 whsc 流头交给 PeekHeader
    // 零块链遍历，O(1) 次 I/O，专供列表页快速预览头字段
    bool PeekRootBlock(uint32_t rootBlock, std::vector<uint8_t>& out, std::string& err) const;
    // 统计块链条数
    bool CountChain(uint32_t rootBlock, uint32_t& blocks, std::string& err) const;

    // 读取并解码单个条目
    bool ReadEntry(const Entry& entry, std::vector<uint8_t>& plain, WhscInfo& info, std::string& err) const;

    // 解包全部条目到目录；cb 可为空
    bool ExtractAll(const std::wstring& outDir, const EntryResultCallback& cb, const ProgressCallback& progress,
                    int& okCount, int& failCount, std::string& err);
    // 解包单个条目
    bool ExtractEntry(const Entry& entry, const std::wstring& dstPath, std::string& err) const;

private:
    bool ReadAt(uint64_t offset, size_t count, std::vector<uint8_t>& out, std::string& err) const;

    FILE* fp_ = nullptr;
    std::wstring path_;
    uint64_t size_ = 0;
    uint32_t treeRoot_ = 0;
    uint32_t namesRoot_ = 0;
    bool entriesLoaded_ = false;
    std::vector<Entry> entries_;
};

}  // namespace dpk