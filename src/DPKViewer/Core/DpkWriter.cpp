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

// 从内存模板包按块号链读出虚拟文件原始字节（whsc 流）
bool ReadTplChainStream(const std::vector<uint8_t>& tpl, const std::vector<uint32_t>& chain,
                        std::vector<uint8_t>& stream, std::string& err) {
    stream.clear();
    if (chain.empty()) {
        err = "空块链";
        return false;
    }
    const size_t blockCount = tpl.size() / kBlockSize;
    if (chain[0] >= blockCount) {
        err = "根块越界";
        return false;
    }
    const uint8_t* root = &tpl[(size_t)chain[0] * kBlockSize];
    const uint32_t fileSize = (uint32_t)root[12] | ((uint32_t)root[13] << 8) | ((uint32_t)root[14] << 16) |
                              ((uint32_t)root[15] << 24);
    if ((uint64_t)fileSize > (kBlockSize - 32) + (uint64_t)(chain.size() - 1) * (kBlockSize - 8)) {
        err = "虚拟文件长度超出块链容量";
        return false;
    }
    stream.reserve(fileSize);
    const size_t take = fileSize < (kBlockSize - 32) ? fileSize : (kBlockSize - 32);
    stream.insert(stream.end(), root + 32, root + 32 + take);
    for (size_t i = 1; i < chain.size() && stream.size() < fileSize; ++i) {
        if (chain[i] >= blockCount) {
            err = "块号越界";
            stream.clear();
            return false;
        }
        const uint8_t* blk = &tpl[(size_t)chain[i] * kBlockSize];
        const size_t rem = fileSize - stream.size();
        const size_t t2 = rem < (kBlockSize - 8) ? rem : (kBlockSize - 8);
        stream.insert(stream.end(), blk + 8, blk + 8 + t2);
    }
    if (stream.size() != fileSize) {
        err = "数据不足";
        stream.clear();
        return false;
    }
    return true;
}

// 模板复用：解码原包流得到明文，与新内容一致时直接复用原包字节
// （保留原包的 flags 与压缩输出，避免重新编码导致存储参数/大小漂移）
bool ReuseTemplateStream(const std::vector<uint8_t>& tpl, const std::vector<uint32_t>& chain,
                         const std::vector<uint8_t>& plain, std::vector<uint8_t>& streamOut) {
    std::vector<uint8_t> orig;
    std::string e1;
    if (!ReadTplChainStream(tpl, chain, orig, e1)) return false;
    std::vector<uint8_t> origPlain;
    WhscInfo info;
    std::string e2;
    if (!WhscDecode(orig, origPlain, info, e2)) return false;  // 模板条目损坏：走重新编码
    if (origPlain.size() != plain.size()) return false;
    if (!plain.empty() && memcmp(&plain[0], &origPlain[0], plain.size()) != 0) return false;
    streamOut.swap(orig);
    return true;
}

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

// 文件版链遍历：逐块 seek 只读 8 字节头，不把整包读进内存（大包必需）
bool WalkChainFile(FILE* fp, uint64_t fsize, uint32_t root, std::vector<uint32_t>& out) {
    out.clear();
    if (root == 0) return false;
    const uint32_t total = (uint32_t)(fsize / kBlockSize);
    uint32_t bn = root;
    uint8_t hdr[4];
    while (bn != 0 && bn < total && out.size() < total) {
        out.push_back(bn);
        if (_fseeki64(fp, (__int64)bn * kBlockSize + 4, SEEK_SET) != 0 || fread(hdr, 1, 4, fp) != 4) return false;
        bn = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) | ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    }
    return !out.empty();
}

}  // namespace

bool LoadPackLayout(const std::wstring& templateDpkPath, PackLayout& layout, std::string& err,
                    const ProgressCallback& progress) {
    layout = PackLayout();
    layout.templatePath = templateDpkPath;

    if (progress && !progress(L"打开包", 0)) {
        err = "操作已取消";
        return false;
    }
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

    // 逐块 seek 遍历链：不把整包读进内存（大包必需）
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, templateDpkPath.c_str(), L"rb") != 0 || fp == nullptr) {
        err = "打开模板包失败";
        return false;
    }
    const uint64_t fsize = reader.FileSize();
    if (!WalkChainFile(fp, fsize, layout.treeRoot, layout.treeBlocks)) {
        fclose(fp);
        err = "模板包 treeRoot 块链无效";
        return false;
    }
    if (!WalkChainFile(fp, fsize, layout.namesRoot, layout.namesBlocks)) {
        fclose(fp);
        err = "模板包 namesRoot 块链无效";
        return false;
    }

    const std::vector<Entry>& entries = reader.Entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        // 大包逐块扫描是主要耗时：每 64 条报告一次进度并泵消息，Esc 可取消
        if (progress && (i & 63) == 0) {
            const int pct = entries.empty() ? 100 : (int)(i * 100 / entries.size());
            if (!progress(Utf8ToWide("扫描块布局: " + entries[i].pathUtf8), pct)) {
                fclose(fp);
                err = "操作已取消";
                return false;
            }
        }
        std::string key = entries[i].pathUtf8;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)tolower(c); });
        layout.entryOrder.push_back(key);  // LoadEntries 顺序即原包目录树顺序
        PackLayoutEntry le;
        le.rootBlock = entries[i].rootBlock;
        le.blocks = entries[i].blockCount;
        WalkChainFile(fp, fsize, entries[i].rootBlock, le.chain);
        layout.byPath[key] = le;
    }

    fclose(fp);

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

    // 模板包与其整体布局提前加载：编码阶段就要尝试逐条目复用原包字节
    const PackLayout* layout = (opt.layout && opt.layout->totalBlocks > 0) ? opt.layout.get() : nullptr;
    std::vector<uint8_t> tplBytes;
    if (layout && !ReadFileBytes(layout->templatePath, tplBytes, err)) {
        err = "读取模板包失败: " + err;
        return false;
    }
    // 条目路径（小写）-> 原包块链，复用判定用
    auto findTplChain = [&](const std::wstring& relPath) -> const std::vector<uint32_t>* {
        if (!layout) return nullptr;
        std::string key = WideToUtf8(relPath);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)tolower(c); });
        auto it = layout->byPath.find(key);
        if (it == layout->byPath.end() || it->second.chain.empty()) return nullptr;
        return &it->second.chain;
    };

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

    // 模板模式：按原包目录树顺序排列条目——目录树/路径表明文与原包一致时可整体复用
    if (layout) {
        std::map<std::string, size_t> orderOf;
        for (size_t i = 0; i < layout->entryOrder.size(); ++i) orderOf[layout->entryOrder[i]] = i;
        auto tplRank = [&](const PackFile& f) -> size_t {
            std::string key = WideToUtf8(f.relPath);
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)tolower(c); });
            auto it = orderOf.find(key);
            return it != orderOf.end() ? it->second : (size_t)-1;
        };
        std::stable_sort(items.begin(), items.end(), [&](const PackFile& a, const PackFile& b) {
            const size_t ra = tplRank(a);
            const size_t rb = tplRank(b);
            if (ra != rb) return ra < rb;  // 模板条目按原包顺序在前，新增条目保持原序在后
            return false;
        });
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
        const std::vector<uint32_t>* tplChain = findTplChain(items[i].relPath);
        if (tplChain && ReuseTemplateStream(tplBytes, *tplChain, items[i].data, p.stream)) {
            ++result.reusedCount;  // 内容未变：原样复用原包字节（保留原 flags 与压缩输出）
        } else if (!WhscEncode(items[i].data, flags, p.stream, eerr, opt.level, opt.compress)) {
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
    if (!WhscEncode(treeRaw, flags, treeStream, eerr, opt.level, opt.compress)) {
        err = "目录树编码失败: " + eerr;
        return false;
    }
    // names 内容只含路径字符串，与 rootBlock 无关，只需编码一次；模板一致时直接复用
    if (layout && ReuseTemplateStream(tplBytes, layout->namesBlocks, namesBlob, namesStream)) {
        // 原样复用原包路径名表
    } else if (!WhscEncode(namesBlob, flags, namesStream, eerr, opt.level, opt.compress)) {
        err = "路径名表编码失败: " + eerr;
        return false;
    }

    // ---------- 阶段 4：块布局 ----------
    // layout 模式：严格沿用原包块号回填，保证总块数与结构一致
    // 普通模式：treeRoot=1、namesRoot=2，文件块从 3 起连续排，续块放在文件之后
    const uint32_t treeRoot = layout ? layout->treeRoot : 1;
    const uint32_t namesRoot = layout ? layout->namesRoot : 2;
    uint32_t treeBlocks = BlocksFor(treeStream.size());
    uint32_t namesBlocks = BlocksFor(namesStream.size());

    std::vector<uint32_t> treeBN, namesBN;  // 完整链（含根块）
    uint32_t totalBlocks = 0;

    for (int iter = 0; iter < 6; ++iter) {
        treeBN.clear();
        namesBN.clear();

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
            // 链缩短时多出的原块不置空：保留模板原字节（已不可达，与原包残留一致）
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
                    // 原链多出的块不再置空：保留模板原字节（不可达，与原包残留一致）
                    prepared[i].blockNumbers.assign(it->second.chain.begin(),
                                                    it->second.chain.begin() + prepared[i].blocks);
                    prepared[i].rootBlock = prepared[i].blockNumbers[0];
                    placed = true;
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
        // 与模板目录树内容一致时复用原包流（字节级），否则重编码
        std::vector<uint8_t> reusedTree;
        if (layout && ReuseTemplateStream(tplBytes, layout->treeBlocks, treeRawReal, reusedTree)) {
            treeStream.swap(reusedTree);
        } else if (!WhscEncode(treeRawReal, flags, treeStream, eerr, opt.level, opt.compress)) {
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

namespace {

// 把一条流按块链直接写入已打开的包文件（与 FillBlockChain 同构，但不占内存缓冲整包）
bool WriteChainToFile(FILE* fp, const std::vector<uint8_t>& stream, const std::vector<uint32_t>& chain) {
    const uint32_t total = (uint32_t)chain.size();
    if (total == 0) return true;
    std::vector<uint8_t> block(kBlockSize, 0);
    WriteU32(&block[0], 0);                                    // prev = 0，根块
    WriteU32(&block[4], total > 1 ? chain[1] : 0);             // next
    WriteU32(&block[8], chain.back());                         // tail
    WriteU32(&block[12], (uint32_t)stream.size());             // fileSize
    const size_t take = stream.size() < (kBlockSize - 32) ? stream.size() : (kBlockSize - 32);
    if (take) memcpy(&block[32], &stream[0], take);
    if (_fseeki64(fp, (__int64)chain[0] * kBlockSize, SEEK_SET) != 0 ||
        fwrite(&block[0], 1, kBlockSize, fp) != kBlockSize) {
        return false;
    }
    size_t offset = take;
    for (uint32_t i = 1; i < total; ++i) {
        std::fill(block.begin(), block.end(), 0);
        WriteU32(&block[0], chain[i - 1]);
        WriteU32(&block[4], (i + 1 < total) ? chain[i + 1] : 0);
        const size_t remaining = stream.size() - offset;
        const size_t count = remaining < (kBlockSize - 8) ? remaining : (kBlockSize - 8);
        if (count) memcpy(&block[8], &stream[offset], count);
        offset += count;
        if (_fseeki64(fp, (__int64)chain[i] * kBlockSize, SEEK_SET) != 0 ||
            fwrite(&block[0], 1, kBlockSize, fp) != kBlockSize) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool PatchEntryInPackage(const std::wstring& pkgPath, const std::string& pathUtf8,
                         const std::vector<uint8_t>& data, bool compress, bool encrypt, int level,
                         uint32_t& entryCount, uint32_t& totalBlocksOut, bool& replaced, std::string& err,
                         const ProgressCallback& progress) {
    entryCount = 0;
    totalBlocksOut = 0;
    replaced = false;
    if (progress && !progress(L"打开包", 0)) {
        err = "操作已取消";
        return false;
    }

    Reader reader;
    if (!reader.Open(pkgPath, err)) {
        err = "打开包失败: " + err;
        return false;
    }
    if (!reader.LoadEntries(err, false)) {
        err = "解析目录失败: " + err;
        return false;
    }
    PackLayout layout;
    // 布局扫描是主要耗时阶段，映射到总进度 5%..55%（progress 空时直通不回调）
    const bool hasProgress = (bool)progress;
    if (!LoadPackLayout(pkgPath, layout, err,
                        hasProgress ? ProgressCallback([progress](const std::wstring& m, int p) {
                            return progress(m, 5 + p / 2);
                        })
                                    : ProgressCallback())) {
        return false;
    }
    // 拷贝条目表后立刻关闭只读句柄：fopen_s 打开的文件不可共享写入，写阶段要用 r+b 重开
    const std::vector<Entry> entries = reader.Entries();
    reader.Close();
    entryCount = (uint32_t)entries.size();

    // 定位同名条目（忽略大小写；替换时保留原路径字节，树/路径表无需变动）
    const std::string key = ToLowerAscii(pathUtf8);
    int hit = -1;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (ToLowerAscii(entries[i].pathUtf8) == key) {
            hit = (int)i;
            break;
        }
    }
    replaced = hit >= 0;

    // 编码新流（自适应压缩）
    const uint8_t flags = (uint8_t)((compress ? 0x01 : 0x00) | (encrypt ? 0x02 : 0x00));
    std::vector<uint8_t> stream;
    std::string eerr;
    if (!WhscEncode(data, flags, stream, eerr, level, compress)) {
        err = "编码失败: " + eerr;
        return false;
    }
    const uint32_t newBlocks = BlocksFor(stream.size());

    // 块分配：优先原链前缀；不足取空闲块（原包历史残留），再不足向文件尾扩展
    uint32_t totalBlocks = layout.totalBlocks;
    size_t freeIdx = 0;
    auto allocBlock = [&]() -> uint32_t {
        if (freeIdx < layout.freeBlocks.size()) return layout.freeBlocks[freeIdx++];
        return totalBlocks++;
    };

    std::vector<uint32_t> entryChain;
    if (hit >= 0) {
        auto it = layout.byPath.find(key);
        if (it != layout.byPath.end()) entryChain = it->second.chain;
    }
    if (newBlocks > entryChain.size()) {
        while (entryChain.size() < newBlocks) entryChain.push_back(allocBlock());
    } else {
        entryChain.resize(newBlocks);  // 缩短：原链多出的块保留原字节（不可达）
    }
    const uint32_t rootBlock = entryChain.empty() ? 0 : entryChain[0];

    // 目录树/路径表：替换时 rootBlock 不变 -> 树明文不变，完全不用重写；
    // 新增条目时按原包顺序重建（新条目追加在末尾）
    std::vector<uint8_t> treeStream, namesStream;
    std::vector<uint32_t> treeChain, namesChain;
    if (!replaced) {
        std::map<std::string, const Entry*> byLower;
        for (const Entry& e : entries) byLower[ToLowerAscii(e.pathUtf8)] = &e;
        std::vector<PreparedForBuild> files;
        files.reserve(entries.size() + 1);
        for (const std::string& okey : layout.entryOrder) {
            auto it = byLower.find(okey);
            if (it == byLower.end()) continue;
            PreparedForBuild f;
            f.relPath = Utf8ToWide(it->second->pathUtf8);
            f.rootBlock = it->second->rootBlock;
            files.push_back(f);
        }
        PreparedForBuild f;
        f.relPath = Utf8ToWide(pathUtf8);
        f.rootBlock = rootBlock;
        files.push_back(f);

        std::vector<uint8_t> treeRaw, namesBlob;
        BuildTreeAndNames(files, treeRaw, namesBlob);
        if (!WhscEncode(treeRaw, flags, treeStream, eerr, level, compress) ||
            !WhscEncode(namesBlob, flags, namesStream, eerr, level, compress)) {
            err = "目录树编码失败: " + eerr;
            return false;
        }
        // 链长必须精确等于流所需块数：读取端按 fileSize 提前停链，多出的块会造成尾块校验失败
        treeChain = layout.treeBlocks;
        const uint32_t treeNeed = BlocksFor(treeStream.size());
        if (treeChain.size() > treeNeed) {
            treeChain.resize(treeNeed);  // 缩短：原链多出的块保留原字节（不可达）
        } else {
            while (treeChain.size() < treeNeed) treeChain.push_back(allocBlock());
        }
        namesChain = layout.namesBlocks;
        const uint32_t namesNeed = BlocksFor(namesStream.size());
        if (namesChain.size() > namesNeed) {
            namesChain.resize(namesNeed);
        } else {
            while (namesChain.size() < namesNeed) namesChain.push_back(allocBlock());
        }
        entryCount += 1;
    }

    if (progress && !progress(Utf8ToWide("写入 " + pathUtf8), 60)) {
        err = "操作已取消";
        return false;
    }

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, pkgPath.c_str(), L"r+b") != 0 || fp == nullptr) {
        err = "打开包失败（写入）：包文件正被其他程序占用，请关闭后重试";
        return false;
    }
    bool ok = WriteChainToFile(fp, stream, entryChain);
    if (ok && !replaced) {
        ok = WriteChainToFile(fp, treeStream, treeChain) && WriteChainToFile(fp, namesStream, namesChain);
    }
    if (ok) {
        // 头部总块数（可能已扩展）；treeRoot/namesRoot 沿用原链前缀不变
        uint8_t hdr4[4];
        WriteU32(hdr4, totalBlocks);
        ok = _fseeki64(fp, 0x1C, SEEK_SET) == 0 && fwrite(hdr4, 1, 4, fp) == 4;
    }
    if (ok) {
        // 文件长度补齐到 totalBlocks 块（扩展出的空隙由文件系统零填充）
        _fseeki64(fp, 0, SEEK_END);
        const __int64 want = (__int64)totalBlocks * kBlockSize;
        if (_ftelli64(fp) < want) {
            ok = _fseeki64(fp, want - 1, SEEK_SET) == 0 && fputc(0, fp) != EOF;
        }
    }
    fclose(fp);
    if (!ok) {
        err = "写入包文件失败（包可能已损坏，请从备份恢复）";
        return false;
    }
    totalBlocksOut = totalBlocks;
    if (progress) progress(L"完成", 100);
    return true;
}

}  // namespace dpk
