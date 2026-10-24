// Cli.cpp - 命令行批处理实现
//
// 用法:
//   DPKViewer.exe /list    <包>
//   DPKViewer.exe /extract <包> <目录>
//   DPKViewer.exe /pack    <源目录> <输出包> [/level N] [/nocompress] [/noencrypt] [/template <包>]
// 附加:
//   /log <文件>      同时把输出写入该文件（UTF-8 带 BOM）
//   /quiet           不向控制台输出（/log 仍生效）
#include "Cli.h"

#include <cstdio>
#include <vector>

#include "AppUtil.h"

#include "Core/DpkReader.h"
#include "Core/DpkWriter.h"
#include "Core/FileIO.h"
#include "Core/StrUtil.h"
#include "Core/Whsc.h"

namespace app {

namespace {

std::vector<std::wstring> SplitArgs(const std::wstring& cmdLine) {
    std::vector<std::wstring> out;
    std::wstring cur;
    bool inQuote = false;
    for (size_t i = 0; i < cmdLine.size(); ++i) {
        const wchar_t c = cmdLine[i];
        if (c == L'"') {
            inQuote = !inQuote;
        } else if ((c == L' ' || c == L'\t') && !inQuote) {
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

std::wstring Lower(std::wstring s) {
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] >= L'A' && s[i] <= L'Z') s[i] = (wchar_t)(s[i] - L'A' + L'a');
    }
    return s;
}

std::string FlagsText(const dpk::WhscInfo& info) {
    std::string s;
    if (info.compressed) s += "压缩";
    if (info.encrypted) {
        if (!s.empty()) s += "+";
        s += info.contentPassword ? "加密(内容口令)" : "加密(包口令)";
    }
    if (s.empty()) s = "原样";
    return s;
}

int CliList(const std::wstring& pkg, Reporter& out) {
    dpk::Reader reader;
    std::string err;
    if (!reader.Open(pkg, err)) {
        out.Line("FAIL 打开包失败: " + err);
        return 1;
    }
    if (!reader.LoadEntries(err)) {
        out.Line("FAIL 解析目录失败: " + err);
        return 1;
    }
    char head[256];
    _snprintf_s(head, sizeof(head), _TRUNCATE, "包: %s | 大小 %llu 字节 | 总块数 %u | treeRoot=%u | namesRoot=%u | 条目 %llu 条",
                dpk::WideToUtf8(pkg).c_str(), (unsigned long long)reader.FileSize(), reader.TotalBlocks(),
                reader.TreeRoot(), reader.NamesRoot(), (unsigned long long)reader.Entries().size());
    out.Line(std::string(head));

    const std::vector<dpk::Entry>& entries = reader.Entries();
    int bad = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        std::vector<uint8_t> stream;
        dpk::WhscInfo info;
        std::string herr;
        bool okHeader = reader.ReadVirtualFile(entries[i].rootBlock, stream, herr) && dpk::WhscPeekHeader(stream, info, herr);
        char line[1024];
        if (okHeader) {
            _snprintf_s(line, sizeof(line), _TRUNCATE,
                        "[%4llu] 根块 %-6u 块数 %-4u 明文 %-10llu 存储 %-10llu flags=0x%02X %-20s %s",
                        (unsigned long long)(i + 1), entries[i].rootBlock, entries[i].blockCount,
                        (unsigned long long)info.plainSize, (unsigned long long)info.storedSize, info.flags,
                        FlagsText(info).c_str(), entries[i].pathUtf8.c_str());
        } else {
            ++bad;
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[%4llu] 根块 %-6u 块数 %-4u 头部读取失败: %s | %s",
                        (unsigned long long)(i + 1), entries[i].rootBlock, entries[i].blockCount, herr.c_str(),
                        entries[i].pathUtf8.c_str());
        }
        out.Line(std::string(line));
    }
    out.Line("OK 共 " + std::to_string(entries.size()) + " 条，其中头部异常 " + std::to_string(bad) + " 条");
    return bad == 0 ? 0 : 1;
}

int CliExtract(const std::wstring& pkg, const std::wstring& outDir, Reporter& out) {
    dpk::Reader reader;
    std::string err;
    if (!reader.Open(pkg, err) || !reader.LoadEntries(err)) {
        out.Line("FAIL 打开包失败: " + err);
        return 1;
    }
    int okCount = 0;
    int failCount = 0;
    std::vector<std::string> failures;
    const int rc = [&]() -> int {
        dpk::EntryResultCallback cb = [&](const std::string& pathUtf8, bool ok, const std::wstring& message) -> bool {
            if (ok) {
                out.Line("  ok    " + pathUtf8);
            } else {
                out.Line("  FAIL  " + pathUtf8 + " - " + dpk::WideToUtf8(message));
                failures.push_back(pathUtf8);
            }
            return true;
        };
        std::string xerr;
        if (!reader.ExtractAll(outDir, cb, dpk::ProgressCallback(), okCount, failCount, xerr)) {
            out.Line("FAIL 解包失败: " + xerr);
            return 1;
        }
        return 0;
    }();
    if (rc != 0) return 1;
    out.Line("OK 解包完成: 成功 " + std::to_string(okCount) + " 条，失败 " + std::to_string(failCount) + " 条 -> " +
             dpk::WideToUtf8(outDir));
    return failCount == 0 ? 0 : 1;
}

int CliPack(const std::vector<std::wstring>& args, Reporter& out) {
    if (args.size() < 2) {
        out.Line("FAIL /pack 需要 <源目录> <输出包>");
        return 1;
    }
    const std::wstring srcDir = args[0];
    const std::wstring outPkg = args[1];

    dpk::PackOptions opt;
    opt.compress = true;
    opt.encrypt = true;
    opt.level = 9;
    for (size_t i = 2; i < args.size(); ++i) {
        const std::wstring a = Lower(args[i]);
        if (a == L"/level" && i + 1 < args.size()) {
            opt.level = _wtoi(args[++i].c_str());
        } else if (a == L"/nocompress") {
            opt.compress = false;
        } else if (a == L"/noencrypt") {
            opt.encrypt = false;
        } else if (a == L"/template" && i + 1 < args.size()) {
            const std::wstring tpl = args[++i];
            std::string terr;
            if (!dpk::LoadTemplateHeader(tpl, opt.templateHeader, terr)) {
                out.Line("FAIL 读取模板包头失败: " + terr);
                return 1;
            }
            // 同时载入原包布局：沿用原包块号回填，保证总块数与结构一致
            auto layout = std::make_shared<dpk::PackLayout>();
            if (!dpk::LoadPackLayout(tpl, *layout, terr)) {
                out.Line("FAIL 读取模板布局失败: " + terr);
                return 1;
            }
            opt.layout = layout;
        } else {
            out.Line("FAIL 无法识别的选项: " + dpk::WideToUtf8(args[i]));
            return 1;
        }
    }

    dpk::PackResult result;
    std::string err;
    if (!dpk::PackFolder(srcDir, outPkg, opt, result, err, dpk::ProgressCallback())) {
        out.Line("FAIL 生成失败: " + err);
        return 1;
    }
    char line[512];
    _snprintf_s(line, sizeof(line), _TRUNCATE,
                "OK 生成完成: 条目 %u，跳过 %u，明文 %llu 字节，包大小 %llu 字节，总块数 %u（压缩=%s 加密=%s 模板包头=%s）",
                result.entryCount, result.skippedCount, (unsigned long long)result.plainBytes,
                (unsigned long long)result.packageSize, result.totalBlocks, opt.compress ? "是" : "否",
                opt.encrypt ? "是" : "否", opt.templateHeader.empty() ? "无" : "有");
    out.Line(std::string(line));
    if (result.entryCount == 0) {
        out.Line("FAIL 源目录中没有可打包的文件");
        return 1;
    }
    // 立刻回读校验，确保生成的包至少能被本工具自身解开
    dpk::Reader reader;
    std::string rerr;
    if (!reader.Open(outPkg, rerr) || !reader.LoadEntries(rerr)) {
        out.Line("FAIL 生成的包无法读回: " + rerr);
        return 1;
    }
    out.Line("OK 回读校验: 条目 " + std::to_string(reader.Entries().size()) + " 条，总块数 " +
             std::to_string(reader.TotalBlocks()));
    return 0;
}

void CliUsage(const std::vector<std::wstring>& args, Reporter& out) {
    out.Line("用法: DPKViewer.exe /list    <包>");
    out.Line("      DPKViewer.exe /extract <包> <目录>");
    out.Line("      DPKViewer.exe /pack    <源目录> <输出包> [/level N] [/nocompress] [/noencrypt] [/template <包>]");
    out.Line("      /log <文件>  /quiet");
    out.Line("参数 " + std::to_string(args.size()) + " 个，缺少必要的包路径");
}

}  // namespace

int RunCli(const std::wstring& cmdLine) {
    const std::vector<std::wstring> all = SplitArgs(cmdLine);
    std::wstring command;
    std::vector<std::wstring> rest;
    std::wstring logPath;
    bool quiet = false;
    for (size_t i = 0; i < all.size(); ++i) {
        const std::wstring a = Lower(all[i]);
        if (a == L"/list" || a == L"/extract" || a == L"/pack" || a == L"-list" || a == L"-extract" || a == L"-pack" ||
            a == L"/?" || a == L"/help" || a == L"-h") {
            command = a;
        } else if (a == L"/log" && i + 1 < all.size()) {
            logPath = all[++i];
        } else if (a == L"/quiet") {
            quiet = true;
        } else {
            rest.push_back(all[i]);
        }
    }
    if (command.empty()) {
        return -1;
    }

    AttachParentConsole();
    if (quiet) {
        FILE* f = nullptr;
        freopen_s(&f, "NUL", "w", stdout);
    }
    Reporter out(logPath);

    if (command == L"/?" || command == L"/help" || command == L"-h") {
        std::vector<std::wstring> dummy;
        CliUsage(dummy, out);
        return 0;
    }
    if (command == L"/list" || command == L"-list") {
        if (rest.empty()) {
            CliUsage(rest, out);
            return 1;
        }
        return CliList(rest[0], out);
    }
    if (command == L"/extract" || command == L"-extract") {
        if (rest.size() < 2) {
            CliUsage(rest, out);
            return 1;
        }
        return CliExtract(rest[0], rest[1], out);
    }
    return CliPack(rest, out);
}

}  // namespace app