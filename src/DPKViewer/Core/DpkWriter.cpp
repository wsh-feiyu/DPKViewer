// DpkWriter.cpp - 按 docs/DPK_FORMAT.md §7 的布局生成 DPK 包
#include "DpkWriter.h"

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <map>

#include "FileIO.h"
#include "Md5.h"
#include "StrUtil.h"
#include "Whsc.h"

namespace dpk {

namespace {

const char kPackageMagic[] = "whpackage1.0";

void WriteU32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

struct Prepared {
    std::wstring relPath;          // 统一为 '/' 分隔
    std::vector<uint8_t> stream;   // whsc 流
    uint32_t rootBlock = 0;
    uint32_t blocks = 0;
};

// 把一条虚拟文件写成连续块并追加到输出
void AppendVirtualFile(std::vector<uint8_t>& out, const std::vector<uint8_t>& stream, uint32_t rootBlock) {
    const uint32_t blocks = BlocksFor(stream.size());
    std::vector<uint8_t> root(kBlockSize, 0);
    WriteU32(&root[0], 0);
    WriteU32(&root[4], blocks > 1 ? rootBlock + 1 : 0);
    WriteU32(&root[8], rootBlock + blocks - 1);
    WriteU32(&root[12], (uint32_t)stream.size());
    size_t take = stream.size() < (kBlockSize - 32) ? stream.size() : (kBlockSize - 32);
    if (take) memcpy(&root[32], &stream[0], take);
    out.insert(out.end(), root.begin(), root.end());

    size_t offset = take;
    for (uint32_t i = 1; i < blocks; ++i) {
        std::vector<uint8_t> block(kBlockSize, 0);
        WriteU32(&block[0], rootBlock + i - 1);
        WriteU32(&block[4], (i + 1 < blocks) ? (rootBlock + i + 1) : 0);
        size_t remaining = stream.size() - offset;
        size_t count = remaining < (kBlockSize - 8) ? remaining : (kBlockSize - 8);
        if (count) memcpy(&block[8], &stream[offset], count);
        offset += count;
        out.insert(out.end(), block.begin(), block.end());
    }
}

std::vector<std::wstring> SplitComponents(const std::wstring& slashPath) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (size_t i = 0; i < slashPath.size(); ++i) {
        wchar_t c = slashPath[i];
        if (c == L'/' || c == L'\\') {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

struct NameTable {
    std::vector<uint8_t> blob;
    std::map<std::wstring, uint32_t> offsets;

    uint32_t OffsetOf(const std::wstring& component) {
        auto it = offsets.find(component);
        if (it != offsets.end()) return it->second;
        std::string bytes = WideToNameBytes(component);
        uint32_t offset = (uint32_t)blob.size();
        blob.insert(blob.end(), bytes.begin(), bytes.end());
        blob.push_back(0);
        offsets[component] = offset;
        return offset;
    }
};

}  // namespace

bool LoadTemplateHeader(const std::wstring& templateDpkPath, std::vector<uint8_t>& header, std::string& err) {
    header.clear();
    std::vector<uint8_t> raw;
    if (!ReadFileBytes(templateDpkPath, raw, err)) return false;
    if (raw.size() < kBlockSize) {
        err = "模板包小于 1024 字节";
        return false;
    }
    if (memcmp(&raw[0], kPackageMagic, sizeof(kPackageMagic) - 1) != 0) {
        err = "模板包魔数不匹配";
        return false;
    }
    header.assign(raw.begin(), raw.begin() + kBlockSize);
    return true;
}

bool PackFiles(const std::vector<PackFile>& files, const std::wstring& outPath, const PackOptions& opt,
               PackResult& result, std::string& err, const ProgressCallback& progress) {
    result = PackResult();
    const uint8_t flags = (uint8_t)((opt.compress ? 0x01 : 0x00) | (opt.encrypt ? 0x02 : 0x00));

    // 规范化并去重（同一相对路径只保留第一个）
    std::vector<PackFile> items;
    std::vector<std::wstring> seen;
    for (size_t i = 0; i < files.size(); ++i) {
        std::wstring rel = NormalizeSlashesW(files[i].relPath);
        std::string relUtf8 = WideToUtf8(rel);
        if (!IsSafeRelativePath(relUtf8)) {
            ++result.skippedCount;
            continue;
        }
        std::wstring key = rel;
        std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        PackFile item;
        item.relPath = rel;
        item.data = files[i].data;
        items.push_back(item);
    }
    std::sort(items.begin(), items.end(),
              [](const PackFile& a, const PackFile& b) { return a.relPath < b.relPath; });

    // 1) 编码各文件内容并分配块号
    std::vector<Prepared> prepared;
    prepared.reserve(items.size());
    uint32_t cursor = 1;
    for (size_t i = 0; i < items.size(); ++i) {
        if (progress) {
            int percent = items.empty() ? 0 : (int)(i * 90 / items.size());
            if (!progress(Utf8ToWide("正在编码 " + WideToUtf8(items[i].relPath)), percent)) {
                err = "操作已取消";
                return false;
            }
        }
        Prepared p;
        p.relPath = items[i].relPath;
        std::string eerr;
        if (!WhscEncode(items[i].data, flags, p.stream, eerr, opt.level)) {
            err = "编码失败 " + WideToUtf8(items[i].relPath) + ": " + eerr;
            return false;
        }
        p.blocks = BlocksFor(p.stream.size());
        p.rootBlock = cursor;
        cursor += p.blocks;
        result.plainBytes += items[i].data.size();
        result.entryCount += 1;
        prepared.push_back(p);
    }

    // 2) 构造目录树与路径名表
    NameTable names;
    std::vector<uint8_t> tree;
    std::vector<std::wstring> dirStack;
    for (size_t i = 0; i < prepared.size(); ++i) {
        std::vector<std::wstring> comps = SplitComponents(prepared[i].relPath);
        if (comps.empty()) continue;
        std::wstring fileName = comps.back();
        comps.pop_back();

        size_t common = 0;
        while (common < comps.size() && common < dirStack.size() && comps[common] == dirStack[common]) ++common;
        while (dirStack.size() > common) {
            tree.push_back(3);
            dirStack.pop_back();
        }
        for (size_t d = common; d < comps.size(); ++d) {
            tree.push_back(2);
            uint8_t buf[4];
            WriteU32(buf, names.OffsetOf(comps[d]));
            tree.insert(tree.end(), buf, buf + 4);
            dirStack.push_back(comps[d]);
        }
        tree.push_back(1);
        uint8_t buf[8];
        WriteU32(buf, names.OffsetOf(fileName));
        WriteU32(buf + 4, prepared[i].rootBlock);
        tree.insert(tree.end(), buf, buf + 8);
    }

    std::vector<uint8_t> treeStream, namesStream;
    std::string eerr;
    if (!WhscEncode(tree, flags, treeStream, eerr, opt.level)) {
        err = "目录树编码失败: " + eerr;
        return false;
    }
    if (!WhscEncode(names.blob, flags, namesStream, eerr, opt.level)) {
        err = "路径名表编码失败: " + eerr;
        return false;
    }
    uint32_t treeRoot = cursor;
    cursor += BlocksFor(treeStream.size());
    uint32_t namesRoot = cursor;
    cursor += BlocksFor(namesStream.size());

    if (progress) {
        if (!progress(L"正在写入文件", 95)) {
            err = "操作已取消";
            return false;
        }
    }

    // 3) 包头
    std::vector<uint8_t> header(kBlockSize, 0);
    if (opt.templateHeader.size() == kBlockSize) {
        header = opt.templateHeader;
    } else {
        memcpy(&header[0], kPackageMagic, sizeof(kPackageMagic) - 1);
        Md5Digest pwdDigest = Md5Compute(std::string(kPackagePassword));
        memcpy(&header[0x20], pwdDigest.data(), 16);
    }
    WriteU32(&header[0x84], treeRoot);
    WriteU32(&header[0x88], namesRoot);

    // 4) 拼装并落盘
    std::vector<uint8_t> out;
    out.reserve((size_t)cursor * kBlockSize);
    out.insert(out.end(), header.begin(), header.end());
    for (size_t i = 0; i < prepared.size(); ++i) {
        AppendVirtualFile(out, prepared[i].stream, prepared[i].rootBlock);
    }
    AppendVirtualFile(out, treeStream, treeRoot);
    AppendVirtualFile(out, namesStream, namesRoot);
    while (out.size() % kBlockSize != 0) out.push_back(0);

    if (out.size() / kBlockSize != cursor) {
        err = "内部错误：块布局与预期不一致";
        return false;
    }

    std::wstring outDir = DirectoryOf(outPath);
    if (!outDir.empty() && !EnsureDirectory(outDir, &err)) {
        err = "创建输出目录失败: " + err;
        return false;
    }
    if (!WriteFileBytes(outPath, &out[0], out.size(), err)) {
        err = "写入包文件失败: " + err;
        return false;
    }
    result.packageSize = out.size();
    result.totalBlocks = cursor;
    if (progress) progress(L"打包完成", 100);
    return true;
}

bool PackFolder(const std::wstring& srcDir, const std::wstring& outPath, const PackOptions& opt, PackResult& result,
                std::string& err, const ProgressCallback& progress) {
    std::vector<std::wstring> files;
    if (!ListFilesRecursive(srcDir, files, err)) {
        err = "枚举源目录失败: " + err;
        return false;
    }
    if (files.empty()) {
        return PackEmpty(outPath, opt, result, err);
    }
    std::vector<PackFile> items;
    items.reserve(files.size());
    for (size_t i = 0; i < files.size(); ++i) {
        if (progress) {
            int percent = (int)(i * 20 / files.size());
            std::wstring label = MakeRelativeSlashPath(srcDir, files[i]);
            if (!progress(Utf8ToWide("读取 " + WideToUtf8(label)), percent)) {
                err = "操作已取消";
                return false;
            }
        }
        PackFile item;
        item.relPath = MakeRelativeSlashPath(srcDir, files[i]);
        if (!ReadFileBytes(files[i], item.data, err)) {
            err = "读取失败 " + WideToUtf8(files[i]) + ": " + err;
            return false;
        }
        items.push_back(item);
    }

    // 把 PackFiles 内部的 0..100 进度映射到 20..100，避免进度条回跳
    ProgressCallback inner;
    if (progress) {
        inner = [&progress](const std::wstring& message, int percent) {
            return progress(message, 20 + percent * 80 / 100);
        };
    }
    return PackFiles(items, outPath, opt, result, err, inner);
}

bool PackEmpty(const std::wstring& outPath, const PackOptions& opt, PackResult& result, std::string& err) {
    std::vector<PackFile> none;
    return PackFiles(none, outPath, opt, result, err, ProgressCallback());
}

}  // namespace dpk