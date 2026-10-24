// DpkWriter.cpp - 生成 DPK 包
// 布局策略：严格复刻原包 —— treeRoot 固定块 1、namesRoot 固定块 2，
// 其后是两条虚拟文件的续块，再往后才是各文件条目的块链。
#include "DpkWriter.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cwctype>
#include <map>

#include "FileIO.h"
#include "Md5.h"
#include "StrUtil.h"
#include "Whsc.h"
#include "DpkReader.h"

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
    std::wstring relPath;
    std::vector<uint8_t> stream;  // whsc 流
    uint32_t rootBlock = 0;
    uint32_t blocks = 0;
    std::vector<uint32_t> blockNumbers;  // 非空时按此序列写块（原包回填用，允许不连续）
};

struct PreparedForBuild {
    std::wstring relPath;
    uint32_t rootBlock = 0;
};

// 按给定块号序列写入一条虚拟文件（块链）到 blocksMap
void FillBlockChain(std::map<uint32_t, std::vector<uint8_t>>& blocksMap,
                    const std::vector<uint8_t>& stream,
                    const std::vector<uint32_t>& blockNumbers) {
    const uint32_t total = (uint32_t)blockNumbers.size();
    if (total == 0) return;

    std::vector<uint8_t> root(kBlockSize, 0);
    WriteU32(&root[0], 0);  // prev = 0，根块标志
    WriteU32(&root[4], total > 1 ? blockNumbers[1] : 0);
    WriteU32(&root[8], blockNumbers.back());  // tail
    WriteU32(&root[12], (uint32_t)stream.size());
    size_t take = stream.size() < (kBlockSize - 32) ? stream.size() : (kBlockSize - 32);
    if (take) memcpy(&root[32], &stream[0], take);
    blocksMap[blockNumbers[0]] = std::move(root);

    size_t offset = take;
    for (uint32_t i = 1; i < total; ++i) {
        std::vector<uint8_t> block(kBlockSize, 0);
        WriteU32(&block[0], blockNumbers[i - 1]);
        WriteU32(&block[4], (i + 1 < total) ? blockNumbers[i + 1] : 0);
        size_t remaining = stream.size() - offset;
        size_t count = remaining < (kBlockSize - 8) ? remaining : (kBlockSize - 8);
        if (count) memcpy(&block[8], &stream[offset], count);
        offset += count;
        blocksMap[blockNumbers[i]] = std::move(block);
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

// 构造目录树原始字节 + 路径名表 blob（一次遍历，保证 offset 一致）
void BuildTreeAndNames(const std::vector<PreparedForBuild>& files,
                       std::vector<uint8_t>& outTreeRaw,
                       std::vector<uint8_t>& outNamesBlob) {
    outTreeRaw.clear();
    NameTable names;
    std::vector<std::wstring> dirStack;
    for (size_t i = 0; i < files.size(); ++i) {
        std::vector<std::wstring> comps = SplitComponents(files[i].relPath);
        if (comps.empty()) continue;
        std::wstring fileName = comps.back();
        comps.pop_back();

        size_t common = 0;
        while (common < comps.size() && common < dirStack.size() && comps[common] == dirStack[common]) ++common;
        while (dirStack.size() > common) {
            outTreeRaw.push_back(3);
            dirStack.pop_back();
        }
        for (size_t d = common; d < comps.size(); ++d) {
            outTreeRaw.push_back(2);
            uint8_t buf[4];
            WriteU32(buf, names.OffsetOf(comps[d]));
            outTreeRaw.insert(outTreeRaw.end(), buf, buf + 4);
            dirStack.push_back(comps[d]);
        }
        outTreeRaw.push_back(1);
        uint8_t buf[8];
        WriteU32(buf, names.OffsetOf(fileName));
        WriteU32(buf + 4, files[i].rootBlock);
        outTreeRaw.insert(outTreeRaw.end(), buf, buf + 8);
    }
    outNamesBlob = std::move(names.blob);
}

}  // namespace

// 遍历模板包中某根块开始的整条块链，按 next 指针收集块号
namespace {

bool WalkChain(const std::vector<uint8_t>& pkg, uint32_t root, std::vector<uint32_t>& out) {
    out.clear();
    if (root == 0) return false;
    const uint32_t total = (uint32_t)(pkg.size() / kBlockSize);
    uint32_t bn = root;
    while (bn != 0 && bn < total && out.size() < total) {
        out.push_back(bn);
        const uint8_t* p = &pkg[(size_t)bn * kBlockSize];
        bn = (uint32_t)p[4] | ((uint32_t)p[5] << 8) | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
    }
    return !out.empty();
}

// 生成"空块"：占位但不属于任何有效数据链
std::vector<uint8_t> MakeEmptyBlock(uint32_t bn) {
    std::vector<uint8_t> block(kBlockSize, 0);
    WriteU32(&block[8], bn);  // tail 指向自身，fileSize = 0
    return block;
}

}  // namespace

bool LoadPackLayout(const std::wstring& templateDpkPath, PackLayout& layout, std::string& err) {
    layout = PackLayout();
    layout.templatePath = templateDpkPath;

    Reader reader;
    if (!reader.Open(templateDpkPath, err)) {
        err = "打开模板包失败: " + err;
        return false;
    }
    if (!reader.LoadEntries(err, true)) {
        err = "解析模板目录失败: " + err;
        return false;
    }
    layout.totalBlocks = reader.TotalBlocks();
    layout.treeRoot = reader.TreeRoot();
    layout.namesRoot = reader.NamesRoot();

    std::vector<uint8_t> pkg;
    if (!ReadFileBytes(templateDpkPath, pkg, err)) {
        err = "读取模板包失败: " + err;
        return false;
    }
    if (!WalkChain(pkg, layout.treeRoot, layout.treeBlocks)) {
        err = "模板包 treeRoot 块链无效";
        return false;
    }
    if (!WalkChain(pkg, layout.namesRoot, layout.namesBlocks)) {
        err = "模板包 namesRoot 块链无效";
        return false;
    }

    const std::vector<Entry>& entries = reader.Entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        std::string key = entries[i].pathUtf8;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)tolower(c); });
        PackLayoutEntry le;
        le.rootBlock = entries[i].rootBlock;
        le.blocks = entries[i].blockCount;
        WalkChain(pkg, entries[i].rootBlock, le.chain);
        layout.byPath[key] = le;
    }

    // 收集原包中未被任何链引用的空闲块（可复用来存放新增/变大内容，从而保持总块数不变）
    std::vector<uint8_t> used(layout.totalBlocks, 0);
    auto markChain = [&used](const std::vector<uint32_t>& chain) {
        for (size_t i = 0; i < chain.size(); ++i) {
            if (chain[i] < used.size()) used[chain[i]] = 1;
        }
    };
    used[0] = 1;  // 包头
    markChain(layout.treeBlocks);
    markChain(layout.namesBlocks);
    for (auto it = layout.byPath.begin(); it != layout.byPath.end(); ++it) markChain(it->second.chain);
    for (uint32_t bn = 1; bn < layout.totalBlocks; ++bn) {
        if (!used[bn]) layout.freeBlocks.push_back(bn);
    }
    return true;
}

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

    // ---------- 阶段 1：规范化 + 去重（保持输入顺序，不排序）----------
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

    // ---------- 阶段 2：编码各文件内容 ----------
    std::vector<Prepared> prepared;
    prepared.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        if (progress) {
            int percent = items.empty() ? 0 : (int)(i * 70 / items.size());
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
        prepared.push_back(p);
        result.plainBytes += items[i].data.size();
        result.entryCount += 1;
    }

    // ---------- 阶段 3：用占位 rootBlock 预构造 tree/names，拿到两条流的块数 ----------
    std::vector<PreparedForBuild> placeholder;
    placeholder.reserve(prepared.size());
    for (size_t i = 0; i < prepared.size(); ++i) {
        PreparedForBuild f;
        f.relPath = prepared[i].relPath;
        f.rootBlock = 0;
        placeholder.push_back(f);
    }
    std::vector<uint8_t> treeRaw, namesBlob;
    BuildTreeAndNames(placeholder, treeRaw, namesBlob);

    std::vector<uint8_t> treeStream, namesStream;
    std::string eerr;
    if (!WhscEncode(treeRaw, flags, treeStream, eerr, opt.level)) {
        err = "目录树编码失败: " + eerr;
        return false;
    }
    // names 内容只含路径字符串，与 rootBlock 无关，只需编码一次
    if (!WhscEncode(namesBlob, flags, namesStream, eerr, opt.level)) {
        err = "路径名表编码失败: " + eerr;
        return false;
    }

    // ---------- 阶段 4：块布局 ----------
    // layout 模式：严格沿用原包块号回填，保证总块数与结构一致
    // 普通模式：treeRoot=1、namesRoot=2，文件块从 3 起连续排，续块放在文件之后
    const PackLayout* layout = (opt.layout && opt.layout->totalBlocks > 0) ? opt.layout.get() : nullptr;
    std::vector<uint8_t> tplBytes;
    if (layout && !ReadFileBytes(layout->templatePath, tplBytes, err)) {
        err = "读取模板包失败: " + err;
        return false;
    }

    const uint32_t treeRoot = layout ? layout->treeRoot : 1;
    const uint32_t namesRoot = layout ? layout->namesRoot : 2;
    uint32_t treeBlocks = BlocksFor(treeStream.size());
    uint32_t namesBlocks = BlocksFor(namesStream.size());

    std::vector<uint32_t> treeBN, namesBN;  // 完整链（含根块）
    std::vector<uint32_t> staleBlocks;      // 链缩短后残留的块，需置空
    uint32_t totalBlocks = 0;

    for (int iter = 0; iter < 6; ++iter) {
        treeBN.clear();
        namesBN.clear();
        staleBlocks.clear();

        // 追加块号分配：优先复用原包空闲块，用尽后再向文件末尾扩展
        uint32_t appendCursor = layout ? layout->totalBlocks : 3;
        size_t freeIndex = 0;
        auto allocBlock = [&]() -> uint32_t {
            if (layout && freeIndex < layout->freeBlocks.size()) return layout->freeBlocks[freeIndex++];
            return appendCursor++;
        };

        if (layout) {
            treeBN = layout->treeBlocks;
            while (treeBN.size() < treeBlocks) treeBN.push_back(allocBlock());
            namesBN = layout->namesBlocks;
            while (namesBN.size() < namesBlocks) namesBN.push_back(allocBlock());
            for (size_t i = treeBlocks; i < layout->treeBlocks.size(); ++i) staleBlocks.push_back(layout->treeBlocks[i]);
            for (size_t i = namesBlocks; i < layout->namesBlocks.size(); ++i) staleBlocks.push_back(layout->namesBlocks[i]);
        }

        // 分配文件条目块号
        for (size_t i = 0; i < prepared.size(); ++i) {
            prepared[i].blockNumbers.clear();
            bool placed = false;
            if (layout) {
                std::string key = WideToUtf8(prepared[i].relPath);
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)tolower(c); });
                auto it = layout->byPath.find(key);
                if (it != layout->byPath.end() && !it->second.chain.empty() &&
                    prepared[i].blocks <= it->second.chain.size()) {
                    // 沿用原包真实块号序列（可能不连续）
                    prepared[i].blockNumbers.assign(it->second.chain.begin(),
                                                    it->second.chain.begin() + prepared[i].blocks);
                    prepared[i].rootBlock = prepared[i].blockNumbers[0];
                    placed = true;
                    // 原链多出的块置空，避免悬垂
                    for (size_t k = prepared[i].blocks; k < it->second.chain.size(); ++k)
                        staleBlocks.push_back(it->second.chain[k]);
                }
            }
            if (!placed) {
                prepared[i].rootBlock = appendCursor;
                for (uint32_t k = 0; k < prepared[i].blocks; ++k)
                    prepared[i].blockNumbers.push_back(allocBlock());
                prepared[i].rootBlock = prepared[i].blockNumbers.empty() ? 0 : prepared[i].blockNumbers[0];
            }
        }

        if (layout) {
            totalBlocks = appendCursor > layout->totalBlocks ? appendCursor : layout->totalBlocks;
        } else {
            const uint32_t T = treeBlocks > 1 ? treeBlocks - 1 : 0;
            const uint32_t N = namesBlocks > 1 ? namesBlocks - 1 : 0;
            const uint32_t contStart = appendCursor;
            treeBN.push_back(treeRoot);
            for (uint32_t i = 0; i < T; ++i) treeBN.push_back(contStart + i);
            namesBN.push_back(namesRoot);
            for (uint32_t i = 0; i < N; ++i) namesBN.push_back(contStart + T + i);
            totalBlocks = contStart + T + N;
        }

        // 用真实 rootBlock 重建 tree 并重编码
        std::vector<PreparedForBuild> real;
        real.reserve(prepared.size());
        for (size_t i = 0; i < prepared.size(); ++i) {
            PreparedForBuild f;
            f.relPath = prepared[i].relPath;
            f.rootBlock = prepared[i].rootBlock;
            real.push_back(f);
        }
        std::vector<uint8_t> treeRawReal, ignoredNames;
        BuildTreeAndNames(real, treeRawReal, ignoredNames);
        if (!WhscEncode(treeRawReal, flags, treeStream, eerr, opt.level)) {
            err = "目录树编码失败: " + eerr;
            return false;
        }
        const uint32_t newTreeBlocks = BlocksFor(treeStream.size());
        if (newTreeBlocks == treeBlocks) break;  // 稳定了
        treeBlocks = newTreeBlocks;
    }

    if (progress) {
        if (!progress(L"正在写入文件", 90)) {
            err = "操作已取消";
            return false;
        }
    }

    // ---------- 阶段 5：生成包头 ----------
    std::vector<uint8_t> header(kBlockSize, 0);
    if (opt.templateHeader.size() == kBlockSize) {
        header = opt.templateHeader;
    } else {
        memcpy(&header[0], kPackageMagic, sizeof(kPackageMagic) - 1);
        Md5Digest pwdDigest = Md5Compute(std::string(kPackagePassword));
        memcpy(&header[0x20], pwdDigest.data(), 16);
        WriteU32(&header[0x10], 2);          // 头部引用的虚拟文件数量
        WriteU32(&header[0x14], kBlockSize); // 块大小
    }
    WriteU32(&header[0x84], treeRoot);
    WriteU32(&header[0x88], namesRoot);
    WriteU32(&header[0x1C], totalBlocks);  // 总块数（原包该字段等于文件大小/1024）

    // ---------- 阶段 6：填充所有块 ----------
    std::map<uint32_t, std::vector<uint8_t>> blocksMap;
    blocksMap[0] = header;

    // 只用链的前 blocks 个块号（layout 模式下原链可能更长）
    std::vector<uint32_t> treeUse(treeBN.begin(), treeBN.begin() + (std::min)(treeBN.size(), (size_t)treeBlocks));
    std::vector<uint32_t> namesUse(namesBN.begin(), namesBN.begin() + (std::min)(namesBN.size(), (size_t)namesBlocks));
    FillBlockChain(blocksMap, treeStream, treeUse);
    FillBlockChain(blocksMap, namesStream, namesUse);

    for (size_t i = 0; i < prepared.size(); ++i) {
        FillBlockChain(blocksMap, prepared[i].stream, prepared[i].blockNumbers);
    }

    // 链缩短后残留的原块：写成空块，避免悬垂指针
    for (size_t i = 0; i < staleBlocks.size(); ++i) {
        blocksMap[staleBlocks[i]] = MakeEmptyBlock(staleBlocks[i]);
    }

    // ---------- 阶段 7：按块号升序拼接 ----------
    std::vector<uint8_t> out;
    out.reserve((size_t)totalBlocks * kBlockSize);
    for (uint32_t bn = 0; bn < totalBlocks; ++bn) {
        auto it = blocksMap.find(bn);
        if (it != blocksMap.end()) {
            out.insert(out.end(), it->second.begin(), it->second.end());
            continue;
        }
        // layout 模式下未覆盖的块：原样拷贝模板包中的块（含历史残留块）
        const size_t off = (size_t)bn * kBlockSize;
        if (layout && off + kBlockSize <= tplBytes.size()) {
            out.insert(out.end(), tplBytes.begin() + off, tplBytes.begin() + off + kBlockSize);
            continue;
        }
        err = "内部错误：块号 " + std::to_string(bn) + " 未被分配";
        return false;
    }

    // ---------- 阶段 8：落盘 ----------
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
    result.totalBlocks = totalBlocks;
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
    return PackFiles(std::vector<PackFile>(), outPath, opt, result, err, ProgressCallback());
}

}  // namespace dpk
