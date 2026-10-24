// DpkCompare.cpp - DPK 包对比实现
#include "DpkCompare.h"

#include <cstring>
#include <map>

#include "DpkReader.h"
#include "StrUtil.h"

namespace dpk {

namespace {

// 读取条目根块并解析流头（含内嵌 MD5 摘要，位于流偏移 16..32）
bool PeekEntryMeta(const Reader& reader, const Entry& entry, CompareSideInfo& side) {
    side.rootBlock = entry.rootBlock;
    std::vector<uint8_t> stream;
    std::string err;
    if (!reader.PeekRootBlock(entry.rootBlock, stream, err)) {
        side.err = err;
        return false;
    }
    if (!WhscPeekHeader(stream, side.header, err)) {
        side.err = err;
        return false;
    }
    memcpy(side.md5, &stream[16], 16);
    side.headerOk = true;
    return true;
}

const char* FlagsBrief(const WhscInfo& info) {
    if (info.compressed && info.encrypted) return "压缩+加密";
    if (info.compressed) return "压缩";
    if (info.encrypted) return "加密";
    return "原样";
}

// 比较两侧流头字段，返回是否一致；diffs 填差异说明（可叠加）
void CompareHeaders(const CompareSideInfo& a, const CompareSideInfo& b, bool& contentSame, std::string& note) {
    contentSame = false;
    bool md5Same = memcmp(a.md5, b.md5, 16) == 0;
    if (a.header.plainSize != b.header.plainSize) {
        note += "明文大小不同";
    }
    if (!md5Same) {
        if (!note.empty()) note += "，";
        note += "MD5 摘要不同";
    }
    if (a.header.flags != b.header.flags) {
        if (!note.empty()) note += "，";
        note += std::string("标志不同(A:") + FlagsBrief(a.header) + " B:" + FlagsBrief(b.header) + ")";
    }
    if (note.empty()) {
        contentSame = true;
        if (a.header.storedSize != b.header.storedSize) {
            // 摘要与标志一致但存储大小不同：内容相同，仅打包参数（如压缩级别）不同
            note = "内容一致，存储参数不同";
        }
    }
}

void FillPackageInfo(Reader& reader, ComparePackageInfo& pkg) {
    pkg.opened = true;
    pkg.err.clear();
    pkg.fileSize = reader.FileSize();
    pkg.totalBlocks = reader.TotalBlocks();
    pkg.treeRoot = reader.TreeRoot();
    pkg.namesRoot = reader.NamesRoot();
    pkg.entryCount = reader.Entries().size();
}

const char* DepthName(CompareDepth depth) {
    switch (depth) {
        case CompareDepth::PathsOnly: return "仅路径";
        case CompareDepth::Metadata: return "标准";
        case CompareDepth::Full: return "完整";
    }
    return "?";
}

}  // namespace

bool ComparePackages(const std::wstring& pathA, const std::wstring& pathB, CompareDepth depth,
                     CompareResult& result, std::string& err, const ProgressCallback& progress) {
    result = CompareResult();

    Reader readerA, readerB;
    std::string oerr;
    if (!readerA.Open(pathA, oerr)) {
        err = "打开包 A 失败: " + oerr;
        return false;
    }
    if (!readerB.Open(pathB, oerr)) {
        err = "打开包 B 失败: " + oerr;
        return false;
    }
    if (progress && !progress(L"解析包 A 目录", 0)) {
        result.completed = false;
        return true;
    }
    if (!readerA.LoadEntries(oerr, false)) {
        err = "解析包 A 目录失败: " + oerr;
        return false;
    }
    if (!readerB.LoadEntries(oerr, false)) {
        err = "解析包 B 目录失败: " + oerr;
        return false;
    }
    FillPackageInfo(readerA, result.pkgA);
    FillPackageInfo(readerB, result.pkgB);

    // 合并两侧路径集合（按字节序排序）；值为两侧条目下标，-1 表示不存在
    // 注意不能用 operator[] 直接赋值：pair 默认构造为 (0,0) 会把「不存在」误标成下标 0
    std::map<std::string, std::pair<int, int>> merged;
    auto slotOf = [&merged](const std::string& path) -> std::pair<int, int>& {
        auto it = merged.find(path);
        if (it == merged.end()) it = merged.emplace(path, std::make_pair(-1, -1)).first;
        return it->second;
    };
    for (size_t i = 0; i < readerA.Entries().size(); ++i) {
        slotOf(readerA.Entries()[i].pathUtf8).first = (int)i;
    }
    for (size_t i = 0; i < readerB.Entries().size(); ++i) {
        slotOf(readerB.Entries()[i].pathUtf8).second = (int)i;
    }

    result.entries.reserve(merged.size());
    size_t done = 0;
    for (auto it = merged.begin(); it != merged.end(); ++it, ++done) {
        if (progress) {
            const int percent = merged.empty() ? 100 : (int)(done * 100 / merged.size());
            if (!progress(Utf8ToWide(it->first), percent)) {
                result.completed = false;
                return true;  // 用户取消，保留部分结果
            }
        }

        CompareEntry row;
        row.pathUtf8 = it->first;
        const int idxA = it->second.first;
        const int idxB = it->second.second;
        row.a.present = idxA >= 0;
        row.b.present = idxB >= 0;

        if (idxA >= 0 && idxB < 0) {
            row.status = CompareStatus::OnlyA;
            row.note = "仅 A 侧存在";
            if (depth != CompareDepth::PathsOnly) PeekEntryMeta(readerA, readerA.Entries()[idxA], row.a);
        } else if (idxA < 0 && idxB >= 0) {
            row.status = CompareStatus::OnlyB;
            row.note = "仅 B 侧存在";
            if (depth != CompareDepth::PathsOnly) PeekEntryMeta(readerB, readerB.Entries()[idxB], row.b);
        } else if (depth == CompareDepth::PathsOnly) {
            row.status = CompareStatus::Same;
        } else if (depth == CompareDepth::Metadata) {
            const bool okA = PeekEntryMeta(readerA, readerA.Entries()[idxA], row.a);
            const bool okB = PeekEntryMeta(readerB, readerB.Entries()[idxB], row.b);
            if (okA && okB) {
                bool contentSame = false;
                std::string note;
                CompareHeaders(row.a, row.b, contentSame, note);
                row.status = note.empty() ? CompareStatus::Same : CompareStatus::Different;
                row.contentSame = contentSame;
                row.note = note;
            } else {
                row.status = CompareStatus::Error;
                if (!okA) row.note = "A 侧流头读取失败: " + row.a.err;
                if (!okB) row.note += (row.note.empty() ? "" : "；") + std::string("B 侧流头读取失败: ") + row.b.err;
            }
        } else {  // Full：两侧完整解码并逐字节比对（解码内部含 MD5 校验）
            std::vector<uint8_t> plainA, plainB;
            WhscInfo infoA, infoB;
            const Entry& eA = readerA.Entries()[idxA];
            const Entry& eB = readerB.Entries()[idxB];
            row.a.rootBlock = eA.rootBlock;
            row.b.rootBlock = eB.rootBlock;
            const bool okA = readerA.ReadEntry(eA, plainA, infoA, row.a.err);
            const bool okB = readerB.ReadEntry(eB, plainB, infoB, row.b.err);
            if (okA) row.a.decoded = true;
            if (okB) row.b.decoded = true;
            if (okA) row.a.header = infoA;
            if (okB) row.b.header = infoB;
            if (okA && okB) {
                const bool bytesSame = plainA.size() == plainB.size() &&
                                      (plainA.empty() || memcmp(&plainA[0], &plainB[0], plainA.size()) == 0);
                const bool flagsSame = row.a.header.flags == row.b.header.flags;
                if (bytesSame && flagsSame) {
                    row.status = CompareStatus::Same;
                } else {
                    row.status = CompareStatus::Different;
                    row.contentSame = bytesSame;
                    if (bytesSame) {
                        row.note = "内容一致，存储参数不同";
                    } else {
                        row.note = plainA.size() == plainB.size() ? "内容不同（同长度，逐字节比对）"
                                                                  : "内容不同（逐字节比对）";
                    }
                    if (!flagsSame) {
                        if (!row.note.empty()) row.note += "，";
                        row.note += std::string("标志不同(A:") + FlagsBrief(row.a.header) + " B:" +
                                    FlagsBrief(row.b.header) + ")";
                    }
                }
            } else {
                row.status = CompareStatus::Error;
                if (!okA) row.note = "A 侧解码失败: " + row.a.err;
                if (!okB) row.note += (row.note.empty() ? "" : "；") + std::string("B 侧解码失败: ") + row.b.err;
                if (!okA && !okB) row.note = "两侧解码均失败（包内损坏）";
            }
        }

        switch (row.status) {
            case CompareStatus::Same: ++result.stats.same; break;
            case CompareStatus::Different: ++result.stats.different; break;
            case CompareStatus::OnlyA: ++result.stats.onlyA; break;
            case CompareStatus::OnlyB: ++result.stats.onlyB; break;
            case CompareStatus::Error: ++result.stats.errors; break;
        }
        result.entries.push_back(std::move(row));
    }
    result.completed = true;
    if (progress && !progress(L"完成", 100)) {
        // 收尾时取消仍视为完成（结果已完整）
    }
    return true;
}

std::string CompareReportText(const CompareResult& result, CompareDepth depth) {
    std::string s;
    s += "DPKViewer 包对比报告\n";
    s += "深度: ";
    s += DepthName(depth);
    s += result.completed ? "\n" : "（用户取消，部分结果）\n";
    s += "\n[包 A]\n";
    if (result.pkgA.opened) {
        s += "  条目 " + FormatUInt(result.pkgA.entryCount) + " 条，大小 " + FormatBytes(result.pkgA.fileSize) +
             "，总块数 " + FormatUInt(result.pkgA.totalBlocks) + "，treeRoot=" + std::to_string(result.pkgA.treeRoot) +
             " namesRoot=" + std::to_string(result.pkgA.namesRoot) + "\n";
    } else {
        s += "  未打开: " + result.pkgA.err + "\n";
    }
    s += "[包 B]\n";
    if (result.pkgB.opened) {
        s += "  条目 " + FormatUInt(result.pkgB.entryCount) + " 条，大小 " + FormatBytes(result.pkgB.fileSize) +
             "，总块数 " + FormatUInt(result.pkgB.totalBlocks) + "，treeRoot=" + std::to_string(result.pkgB.treeRoot) +
             " namesRoot=" + std::to_string(result.pkgB.namesRoot) + "\n";
    } else {
        s += "  未打开: " + result.pkgB.err + "\n";
    }
    s += "\n[统计]\n";
    s += "  相同 " + FormatUInt(result.stats.same) + "，不同 " + FormatUInt(result.stats.different) +
         "，仅 A " + FormatUInt(result.stats.onlyA) + "，仅 B " + FormatUInt(result.stats.onlyB) +
         "，错误 " + FormatUInt(result.stats.errors) + "，合计 " + FormatUInt(result.stats.Total()) + "\n";

    for (size_t i = 0; i < result.entries.size(); ++i) {
        const CompareEntry& e = result.entries[i];
        if (e.status == CompareStatus::Same) continue;
        const char* st = e.status == CompareStatus::Different ? "不同"
                         : e.status == CompareStatus::OnlyA    ? "仅A"
                         : e.status == CompareStatus::OnlyB    ? "仅B"
                                                               : "错误";
        s += "\n[" + std::string(st) + "] " + e.pathUtf8 + "\n";
        auto sideLine = [&](const char* tag, const CompareSideInfo& side) {
            if (!side.present) {
                s += "  " + std::string(tag) + ": 无此条目\n";
                return;
            }
            if (side.headerOk || side.decoded) {
                s += "  " + std::string(tag) + ": " + FlagsBrief(side.header) + "，明文 " +
                     FormatBytes(side.header.plainSize) + "，存储 " + FormatBytes(side.header.storedSize) + "\n";
            } else if (!side.err.empty()) {
                s += "  " + std::string(tag) + ": 读取失败 - " + side.err + "\n";
            } else {
                s += "  " + std::string(tag) + ": 存在\n";
            }
        };
        sideLine("A", e.a);
        sideLine("B", e.b);
        if (!e.note.empty()) s += "  说明: " + e.note + "\n";
    }
    return s;
}

}  // namespace dpk
