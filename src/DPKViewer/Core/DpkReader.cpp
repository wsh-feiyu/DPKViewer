// DpkReader.cpp - 与 dpk.py 的 Dpk 读取逻辑一一对应
#include "DpkReader.h"

#include <cstring>
#include <map>
#include <set>

#include "FileIO.h"
#include "StrUtil.h"

namespace dpk {

namespace {

const char kPackageMagic[] = "whpackage1.0";
const size_t kMagicLen = 12;
const size_t kHeaderOffsetTreeRoot = 0x84;
const size_t kHeaderOffsetNamesRoot = 0x88;

uint32_t ReadU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

}  // namespace

Reader::Reader() {}

Reader::~Reader() { Close(); }

void Reader::Close() {
    if (fp_) {
        fclose(fp_);
        fp_ = nullptr;
    }
    entriesLoaded_ = false;
    entries_.clear();
}

bool Reader::Open(const std::wstring& path, std::string& err) {
    Close();
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || fp == nullptr) {
        err = "无法打开文件";
        return false;
    }
    _fseeki64(fp, 0, SEEK_END);
    __int64 size = _ftelli64(fp);
    _fseeki64(fp, 0, SEEK_SET);
    if (size < (__int64)kBlockSize) {
        fclose(fp);
        err = "文件小于 1024 字节，不是有效的 DPK 包";
        return false;
    }
    std::vector<uint8_t> header;
    header.resize(kBlockSize);
    if (fread(&header[0], 1, kBlockSize, fp) != kBlockSize) {
        fclose(fp);
        err = "读取包头失败";
        return false;
    }
    if (memcmp(&header[0], kPackageMagic, kMagicLen) != 0) {
        fclose(fp);
        err = "魔数不匹配，不是 whpackage1.0 包";
        return false;
    }
    fp_ = fp;
    path_ = path;
    size_ = (uint64_t)size;
    treeRoot_ = ReadU32(&header[kHeaderOffsetTreeRoot]);
    namesRoot_ = ReadU32(&header[kHeaderOffsetNamesRoot]);
    uint32_t total = TotalBlocks();
    if (treeRoot_ == 0 || treeRoot_ >= total || namesRoot_ == 0 || namesRoot_ >= total) {
        err = "包头中的目录树/路径表块号越界";
        Close();
        return false;
    }
    return true;
}

bool Reader::ReadAt(uint64_t offset, size_t count, std::vector<uint8_t>& out, std::string& err) const {
    out.clear();
    if (offset + count > size_) {
        err = "读取越界";
        return false;
    }
    if (_fseeki64(fp_, (__int64)offset, SEEK_SET) != 0) {
        err = "定位失败";
        return false;
    }
    out.resize(count);
    if (count && fread(&out[0], 1, count, fp_) != count) {
        out.clear();
        err = "读取数据失败";
        return false;
    }
    return true;
}

bool Reader::ReadVirtualFile(uint32_t rootBlock, std::vector<uint8_t>& out, std::string& err) const {
    out.clear();
    const uint32_t totalBlocks = TotalBlocks();
    if (rootBlock >= totalBlocks) {
        err = "块号越界";
        return false;
    }
    std::vector<uint8_t> root;
    if (!ReadAt((uint64_t)rootBlock * kBlockSize, kBlockSize, root, err)) return false;
    uint32_t prev = ReadU32(&root[0]);
    uint32_t next = ReadU32(&root[4]);
    uint32_t tail = ReadU32(&root[8]);
    uint32_t fileSize = ReadU32(&root[12]);
    if (prev != 0) {
        err = "块 " + std::to_string(rootBlock) + " 不是根块";
        return false;
    }
    if ((uint64_t)fileSize > (kBlockSize - 32) + (uint64_t)(totalBlocks - 1) * (kBlockSize - 8)) {
        err = "虚拟文件长度超出包容量，包已损坏";
        return false;
    }
    out.reserve(fileSize);
    size_t take = fileSize < (kBlockSize - 32) ? fileSize : (kBlockSize - 32);
    out.insert(out.end(), root.begin() + 32, root.begin() + 32 + take);

    uint32_t cur = next;
    uint32_t last = rootBlock;
    std::set<uint32_t> seen;
    seen.insert(rootBlock);
    while (cur != 0 && out.size() < fileSize) {
        if (cur >= totalBlocks) {
            err = "块链块号越界";
            return false;
        }
        if (seen.find(cur) != seen.end()) {
            err = "块链成环（块 " + std::to_string(cur) + "）";
            return false;
        }
        seen.insert(cur);
        std::vector<uint8_t> block;
        if (!ReadAt((uint64_t)cur * kBlockSize, kBlockSize, block, err)) return false;
        uint32_t blockPrev = ReadU32(&block[0]);
        uint32_t blockNext = ReadU32(&block[4]);
        if (blockPrev != last) {
            err = "块链断裂 " + std::to_string(last) + " -> " + std::to_string(cur);
            return false;
        }
        size_t remaining = fileSize - out.size();
        size_t take2 = remaining < (kBlockSize - 8) ? remaining : (kBlockSize - 8);
        out.insert(out.end(), block.begin() + 8, block.begin() + 8 + take2);
        last = cur;
        cur = blockNext;
    }
    if (last != tail) {
        err = "尾块校验失败";
        return false;
    }
    if (out.size() != fileSize) {
        err = "虚拟文件数据不足";
        return false;
    }
    return true;
}

bool Reader::CountChain(uint32_t rootBlock, uint32_t& blocks, std::string& err) const {
    blocks = 0;
    const uint32_t totalBlocks = TotalBlocks();
    if (rootBlock >= totalBlocks) {
        err = "块号越界";
        return false;
    }
    uint32_t cur = rootBlock;
    std::set<uint32_t> seen;
    while (cur != 0) {
        if (cur >= totalBlocks) {
            err = "块链块号越界";
            return false;
        }
        if (seen.find(cur) != seen.end()) {
            err = "块链成环";
            return false;
        }
        seen.insert(cur);
        ++blocks;
        std::vector<uint8_t> block;
        // 根块与后续块的 next 字段都在块内偏移 4
        if (!ReadAt((uint64_t)cur * kBlockSize, 8, block, err)) return false;
        cur = ReadU32(&block[4]);
        if (blocks > totalBlocks) {
            err = "块链异常";
            return false;
        }
    }
    return true;
}

bool Reader::LoadEntries(std::string& err, bool computeBlockCounts) {
    entries_.clear();
    entriesLoaded_ = false;
    if (!fp_) {
        err = "包未打开";
        return false;
    }

    std::vector<uint8_t> treeStream, namesStream, treeData, namesData;
    if (!ReadVirtualFile(treeRoot_, treeStream, err)) return false;
    if (!ReadVirtualFile(namesRoot_, namesStream, err)) return false;

    WhscInfo info;
    std::string derr;
    if (!WhscDecode(treeStream, treeData, info, derr)) {
        err = "目录树解码失败: " + derr;
        return false;
    }
    if (!WhscDecode(namesStream, namesData, info, derr)) {
        err = "路径名表解码失败: " + derr;
        return false;
    }

    // 路径名表：以起始偏移为键
    std::map<uint32_t, std::string> nameOf;
    size_t i = 0;
    while (i < namesData.size()) {
        size_t j = i;
        while (j < namesData.size() && namesData[j] != 0) ++j;
        if (j >= namesData.size()) {
            err = "路径表损坏（缺少结尾 0）";
            return false;
        }
        nameOf[(uint32_t)i] = std::string(reinterpret_cast<const char*>(&namesData[0]) + i, j - i);
        i = j + 1;
    }

    std::vector<std::string> dirs;
    size_t off = 0;
    while (off < treeData.size()) {
        uint8_t tag = treeData[off++];
        if (tag == 1) {
            if (off + 8 > treeData.size()) {
                err = "目录树损坏（文件记录不完整）";
                return false;
            }
            uint32_t nameOff = ReadU32(&treeData[off]);
            uint32_t block = ReadU32(&treeData[off + 4]);
            off += 8;
            auto it = nameOf.find(nameOff);
            if (it == nameOf.end()) {
                err = "目录树引用了不存在的名称偏移 " + std::to_string(nameOff);
                return false;
            }
            // 目录栈自外向内，顺序拼接为 "a/b/c"，名称分量按包内编码（GBK 优先）解码后再转为 UTF-8
            std::wstring relW;
            for (size_t d = 0; d < dirs.size(); ++d) {
                relW += NameBytesToWide(dirs[d]);
                relW += L'/';
            }
            relW += NameBytesToWide(it->second);
            Entry entry;
            entry.pathUtf8 = WideToUtf8(relW);
            entry.rootBlock = block;
            entries_.push_back(entry);
        } else if (tag == 2) {
            if (off + 4 > treeData.size()) {
                err = "目录树损坏（目录记录不完整）";
                return false;
            }
            uint32_t nameOff = ReadU32(&treeData[off]);
            off += 4;
            auto it = nameOf.find(nameOff);
            if (it == nameOf.end()) {
                err = "目录树引用了不存在的名称偏移 " + std::to_string(nameOff);
                return false;
            }
            dirs.push_back(it->second);
        } else if (tag == 3) {
            if (dirs.empty()) {
                err = "目录树损坏（栈下溢）";
                return false;
            }
            dirs.pop_back();
        } else {
            err = "目录树损坏（未知记录类型 " + std::to_string((int)tag) + "）";
            return false;
        }
    }

    if (computeBlockCounts) {
        for (size_t k = 0; k < entries_.size(); ++k) {
            uint32_t blocks = 0;
            std::string cerr;
            if (CountChain(entries_[k].rootBlock, blocks, cerr)) {
                entries_[k].blockCount = blocks;
            } else {
                entries_[k].blockCount = 0;
            }
        }
    }
    entriesLoaded_ = true;
    return true;
}

bool Reader::ReadEntry(const Entry& entry, std::vector<uint8_t>& plain, WhscInfo& info, std::string& err) const {
    plain.clear();
    std::vector<uint8_t> stream;
    if (!ReadVirtualFile(entry.rootBlock, stream, err)) return false;
    return WhscDecode(stream, plain, info, err);
}

bool Reader::ExtractEntry(const Entry& entry, const std::wstring& dstPath, std::string& err) const {
    std::vector<uint8_t> plain;
    WhscInfo info;
    if (!ReadEntry(entry, plain, info, err)) return false;
    std::wstring dir = DirectoryOf(dstPath);
    if (!EnsureDirectory(dir, &err)) {
        err = "创建目录失败: " + err;
        return false;
    }
    return WriteFileBytes(dstPath, plain.empty() ? nullptr : &plain[0], plain.size(), err);
}

bool Reader::ExtractAll(const std::wstring& outDir, const EntryResultCallback& cb, const ProgressCallback& progress,
                        int& okCount, int& failCount, std::string& err) {
    okCount = 0;
    failCount = 0;
    if (!fp_) {
        err = "包未打开";
        return false;
    }
    if (!entriesLoaded_ && !LoadEntries(err)) return false;
    if (!EnsureDirectory(outDir, &err)) {
        err = "创建输出目录失败: " + err;
        return false;
    }

    const size_t total = entries_.size();
    for (size_t i = 0; i < total; ++i) {
        const Entry& e = entries_[i];
        if (progress) {
            int percent = total ? (int)(i * 100 / total) : 100;
            if (!progress(Utf8ToWide(e.pathUtf8), percent)) {
                return true;  // 用户取消
            }
        }
        std::wstring msgW;
        if (!IsSafeRelativePath(e.pathUtf8)) {
            ++failCount;
            if (cb) {
                if (!cb(e.pathUtf8, false, L"路径不安全，已跳过")) return true;
            }
            continue;
        }
        std::wstring rel = RepoRelativeToWin(Utf8ToWide(e.pathUtf8));
        std::wstring dst = JoinPath(outDir, rel);
        bool ok = false;
        std::string eerr;
        std::vector<uint8_t> plain;
        WhscInfo info;
        if (ReadEntry(e, plain, info, eerr)) {
            std::wstring dir = DirectoryOf(dst);
            std::string derr;
            if (!EnsureDirectory(dir, &derr)) {
                eerr = "创建目录失败: " + derr;
            } else if (!WriteFileBytes(dst, plain.empty() ? nullptr : &plain[0], plain.size(), eerr)) {
                // eerr 已填充
            } else {
                ok = true;
            }
        }
        if (ok) {
            ++okCount;
            msgW = Utf8ToWide("已导出 " + FormatBytes(plain.size()));
        } else {
            ++failCount;
            msgW = Utf8ToWide(eerr);
        }
        if (cb) {
            if (!cb(e.pathUtf8, ok, msgW)) return true;
        }
    }
    if (progress) {
        progress(L"完成", 100);
    }
    return true;
}

}  // namespace dpk