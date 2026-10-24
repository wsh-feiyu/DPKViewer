// TestCore.cpp - DPKViewer 核心模块验证程序（控制台，不依赖 MFC）
//
// 用法: TestCore.exe <workdir>
//   workdir 由 tests/tools/make_test_package.py 生成：
//     des_fixtures.txt       DES 标准块向量 + 变体（pycryptodome 对照）
//     zlib_fixtures/         <name>.bin（原文）与 <name>.z（Python zlib 压缩）
//     packages/ref_*.dpk     由参考实现 dpk.py 生成的包
//     manifest.txt           期望内容清单：md5\tsize\trelpath
//     src/                   原始文件树
//   本程序输出：out/<pkg>/（解包结果）、packed/*.dpk（本实现生成的包）

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Core/DpkReader.h"
#include "Core/DpkWriter.h"
#include "Core/Des.h"
#include "Core/FileIO.h"
#include "Core/Md5.h"
#include "Core/StrUtil.h"
#include "Core/Zlib.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) {
        printf("[ ok ] %s\n", what.c_str());
    } else {
        printf("[FAIL] %s\n", what.c_str());
        ++g_failures;
    }
}

std::vector<uint8_t> HexToBytes(const std::string& hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto v = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        out.push_back((uint8_t)(v(hex[i]) * 16 + v(hex[i + 1])));
    }
    return out;
}

std::string BytesToHex(const std::vector<uint8_t>& data) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < data.size(); ++i) {
        out.push_back(hex[(data[i] >> 4) & 0xF]);
        out.push_back(hex[data[i] & 0xF]);
    }
    return out;
}

std::vector<std::string> SplitWs(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
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

struct ManifestItem {
    std::string md5;
    uint64_t size = 0;
    std::string relPath;
};

std::vector<ManifestItem> LoadManifest(const std::wstring& path) {
    std::vector<ManifestItem> items;
    std::vector<uint8_t> raw;
    std::string err;
    if (!dpk::ReadFileBytes(path, raw, err)) return items;
    std::string text(raw.begin(), raw.end());
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        // 各列以 TAB 分隔：路径本身允许包含空格
        size_t t1 = line.find('\t');
        size_t t2 = (t1 == std::string::npos) ? std::string::npos : line.find('\t', t1 + 1);
        if (t1 != std::string::npos && t2 != std::string::npos) {
            ManifestItem item;
            item.md5 = line.substr(0, t1);
            item.size = strtoull(line.substr(t1 + 1, t2 - t1 - 1).c_str(), nullptr, 10);
            item.relPath = line.substr(t2 + 1);
            items.push_back(item);
        }
        if (end == text.size()) break;
        pos = end + 1;
    }
    return items;
}

bool VerifyExtracted(const std::wstring& outDir, const std::vector<ManifestItem>& manifest, const std::string& tag) {
    bool allOk = true;
    for (size_t i = 0; i < manifest.size(); ++i) {
        std::wstring rel = dpk::RepoRelativeToWin(dpk::Utf8ToWide(manifest[i].relPath));
        std::wstring full = dpk::JoinPath(outDir, rel);
        std::vector<uint8_t> data;
        std::string err;
        if (!dpk::ReadFileBytes(full, data, err)) {
            printf("[FAIL] %s: 缺少文件 %s\n", tag.c_str(), manifest[i].relPath.c_str());
            allOk = false;
            continue;
        }
        if (data.size() != manifest[i].size) {
            printf("[FAIL] %s: %s 大小 %llu != %llu\n", tag.c_str(), manifest[i].relPath.c_str(),
                   (unsigned long long)data.size(), (unsigned long long)manifest[i].size);
            allOk = false;
            continue;
        }
        std::string hex = dpk::Md5ToHex(dpk::Md5Compute(data.empty() ? nullptr : &data[0], data.size()));
        if (hex != manifest[i].md5) {
            printf("[FAIL] %s: %s MD5 %s != %s\n", tag.c_str(), manifest[i].relPath.c_str(), hex.c_str(),
                   manifest[i].md5.c_str());
            allOk = false;
        }
    }
    return allOk;
}

void TestDesFixtures(const std::wstring& workDir) {
    std::vector<uint8_t> raw;
    std::string err;
    if (!dpk::ReadFileBytes(dpk::JoinPath(workDir, L"des_fixtures.txt"), raw, err)) {
        Check(false, "读取 des_fixtures.txt 失败");
        return;
    }
    std::string text(raw.begin(), raw.end());
    size_t pos = 0;
    int ecbCount = 0, varCount = 0, mismatch = 0, roundTripFail = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        if (end == text.size() && line.empty()) break;
        std::vector<std::string> p = SplitWs(line);
        if (p.size() == 4 && p[0] == "ECB") {
            std::vector<uint8_t> key = HexToBytes(p[1]);
            std::vector<uint8_t> plain = HexToBytes(p[2]);
            std::vector<uint8_t> expect = HexToBytes(p[3]);
            uint64_t sk[16];
            dpk::Des::BuildSubkeys(&key[0], sk);
            uint8_t out[8];
            dpk::Des::EncryptBlock(sk, &plain[0], out);
            if (memcmp(out, &expect[0], 8) != 0) ++mismatch;
            uint8_t back[8];
            dpk::Des::DecryptBlock(sk, &expect[0], back);
            if (memcmp(back, &plain[0], 8) != 0) ++mismatch;
            ++ecbCount;
        } else if (p.size() == 5 && p[0] == "VAR") {
            // VAR <key> <plain> <cipher:参考加密结果> <decback:参考解密结果>
            std::vector<uint8_t> key = HexToBytes(p[1]);
            std::vector<uint8_t> plain = HexToBytes(p[2]);
            std::vector<uint8_t> expect = HexToBytes(p[3]);
            std::vector<uint8_t> expectBack = HexToBytes(p[4]);
            uint8_t key8[8];
            memcpy(key8, &key[0], 8);
            std::vector<uint8_t> got = dpk::DesCryptPayload(plain, key8, false);
            if (got != expect) ++mismatch;
            std::vector<uint8_t> back = dpk::DesCryptPayload(expect, key8, true);
            if (back != expectBack) ++mismatch;
            if (back != plain) ++roundTripFail;
            ++varCount;
        }
        if (end == text.size()) break;
        pos = end + 1;
    }
    Check(ecbCount >= 10 && varCount >= 10, "DES 固定向量数量充足");
    Check(mismatch == 0, "DES 向量与 pycryptodome / dpk.py 一致（ECB " + std::to_string(ecbCount) + " 组 + 变体 " +
                             std::to_string(varCount) + " 组）");
    Check(roundTripFail == 0, "DES 变体在参考实现上可逆（解密结果等于原文）");
}

void TestZlibFixtures(const std::wstring& workDir) {
    std::wstring dir = dpk::JoinPath(workDir, L"zlib_fixtures");
    std::vector<std::wstring> files;
    std::string err;
    if (!dpk::ListFilesRecursive(dir, files, err)) {
        Check(false, "读取 zlib_fixtures 目录失败: " + err);
        return;
    }
    int inflateCount = 0, inflateFail = 0;
    for (size_t i = 0; i < files.size(); ++i) {
        std::wstring name = dpk::FileNameOf(files[i]);
        if (name.size() < 3 || name.substr(name.size() - 2) != L".z") continue;
        // 命名规则为 <name>.l<level>.z：先去掉 ".z"，再去掉 ".l<level>"
        std::wstring stem = name.substr(0, name.size() - 2);
        size_t dot = stem.rfind(L'.');
        if (dot == std::wstring::npos) continue;
        std::wstring base = stem.substr(0, dot);  // <name>.l<level>.z -> <name>
        std::vector<uint8_t> packed, expect, got;
        std::string e1, e2;
        if (!dpk::ReadFileBytes(files[i], packed, e1)) continue;
        if (!dpk::ReadFileBytes(dpk::JoinPath(dir, base + L".bin"), expect, e2)) continue;
        std::string zerr;
        if (!dpk::ZlibDecompress(packed.empty() ? nullptr : &packed[0], packed.size(), got, zerr)) {
            printf("[FAIL] 解压 %s 失败: %s\n", dpk::WideToUtf8(name).c_str(), zerr.c_str());
            ++inflateFail;
            continue;
        }
        if (got != expect) {
            printf("[FAIL] 解压 %s 内容不一致\n", dpk::WideToUtf8(name).c_str());
            ++inflateFail;
        }
        ++inflateCount;
    }
    Check(inflateCount >= 6, "zlib 解压样本数量充足（" + std::to_string(inflateCount) + "）");
    Check(inflateFail == 0, "自实现 inflate 与 Python zlib 结果一致");

    // 用本实现压缩，输出 .cppz 供 Python 校验
    int compressCount = 0, compressFail = 0;
    for (size_t i = 0; i < files.size(); ++i) {
        std::wstring name = dpk::FileNameOf(files[i]);
        if (name.size() < 4 || name.substr(name.size() - 4) != L".bin") continue;
        std::vector<uint8_t> plain;
        std::string e1;
        if (!dpk::ReadFileBytes(files[i], plain, e1)) continue;
        for (int level : {0, 9}) {
            std::vector<uint8_t> packed;
            std::string zerr;
            if (!dpk::ZlibCompress(plain.empty() ? nullptr : &plain[0], plain.size(), packed, level, zerr)) {
                ++compressFail;
                continue;
            }
            std::wstring outName = name.substr(0, name.size() - 4) + L".cppz" + std::to_wstring(level);
            std::string werr;
            if (!dpk::WriteFileBytes(dpk::JoinPath(dir, outName), packed.empty() ? nullptr : &packed[0],
                                     packed.size(), werr)) {
                ++compressFail;
                continue;
            }
            // 自回环校验
            std::vector<uint8_t> back;
            std::string derr;
            if (!dpk::ZlibDecompress(packed.empty() ? nullptr : &packed[0], packed.size(), back, derr) || back != plain) {
                ++compressFail;
                continue;
            }
            ++compressCount;
        }
    }
    Check(compressCount >= 12, "自实现 deflate 样本数量充足（" + std::to_string(compressCount) + "）");
    Check(compressFail == 0, "自实现 deflate 往返一致，并已输出 .cppz 供 Python 复核");
}

void TestExtract(const std::wstring& workDir, const std::vector<ManifestItem>& manifest) {
    std::wstring pkgDir = dpk::JoinPath(workDir, L"packages");
    std::vector<std::wstring> files;
    std::string err;
    if (!dpk::ListFilesRecursive(pkgDir, files, err)) {
        Check(false, "读取 packages 目录失败: " + err);
        return;
    }
    int pkgCount = 0;
    for (size_t i = 0; i < files.size(); ++i) {
        std::wstring name = dpk::FileNameOf(files[i]);
        if (name.size() < 4 || name.substr(name.size() - 4) != L".dpk") continue;
        std::wstring base = name.substr(0, name.size() - 4);
        dpk::Reader reader;
        std::string oerr;
        if (!reader.Open(files[i], oerr)) {
            Check(false, "打开参考包失败 " + dpk::WideToUtf8(name) + ": " + oerr);
            continue;
        }
        if (!reader.LoadEntries(oerr)) {
            Check(false, "解析参考包失败 " + dpk::WideToUtf8(name) + ": " + oerr);
            continue;
        }
        // 解包目录沿用完整包文件名（含 .dpk），与 tools/verify.py 的约定 out/<pkg> 保持一致
        std::wstring outDir = dpk::JoinPath(dpk::JoinPath(workDir, L"out"), name);
        int ok = 0, fail = 0;
        std::string xerr;
        if (!reader.ExtractAll(outDir, dpk::EntryResultCallback(), dpk::ProgressCallback(), ok, fail, xerr)) {
            Check(false, "解包参考包失败 " + dpk::WideToUtf8(name) + ": " + xerr);
            continue;
        }
        bool contentOk = VerifyExtracted(outDir, manifest, dpk::WideToUtf8(base));
        Check(fail == 0 && contentOk,
              "参考包 " + dpk::WideToUtf8(name) + " 解包内容与 manifest 一致（条目 " + std::to_string(reader.Entries().size()) +
                  "，成功 " + std::to_string(ok) + "，失败 " + std::to_string(fail) + "）");
        ++pkgCount;
    }
    Check(pkgCount >= 3, "参考包数量充足（" + std::to_string(pkgCount) + "）");
}

void TestPack(const std::wstring& workDir, const std::vector<ManifestItem>& manifest) {
    std::wstring srcDir = dpk::JoinPath(workDir, L"src");
    std::wstring outDir = dpk::JoinPath(workDir, L"packed");
    std::string err;
    if (!dpk::EnsureDirectory(outDir, &err)) {
        Check(false, "创建 packed 目录失败");
        return;
    }
    struct Combo {
        const wchar_t* name;
        bool compress;
        bool encrypt;
        int level;
    };
    const Combo combos[] = {{L"pack_ce.dpk", true, true, 9},
                            {L"pack_ce_stored.dpk", true, true, 0},
                            {L"pack_c.dpk", true, false, 9},
                            {L"pack_e.dpk", false, true, 9},
                            {L"pack_raw.dpk", false, false, 9}};
    for (const Combo& c : combos) {
        dpk::PackOptions opt;
        opt.compress = c.compress;
        opt.encrypt = c.encrypt;
        opt.level = c.level;
        dpk::PackResult result;
        std::string perr;
        std::wstring outPath = dpk::JoinPath(outDir, c.name);
        if (!dpk::PackFolder(srcDir, outPath, opt, result, perr, dpk::ProgressCallback())) {
            Check(false, std::string("打包失败 ") + dpk::WideToUtf8(c.name) + ": " + perr);
            continue;
        }
        // 自回环：用本实现读回并核对内容
        dpk::Reader reader;
        std::string oerr;
        if (!reader.Open(outPath, oerr) || !reader.LoadEntries(oerr)) {
            Check(false, std::string("打包结果无法读回 ") + dpk::WideToUtf8(c.name) + ": " + oerr);
            continue;
        }
        std::wstring exDir = dpk::JoinPath(dpk::JoinPath(workDir, L"out"), (std::wstring(c.name) + L".selfcheck"));
        int ok = 0, fail = 0;
        std::string xerr;
        reader.ExtractAll(exDir, dpk::EntryResultCallback(), dpk::ProgressCallback(), ok, fail, xerr);
        bool contentOk = VerifyExtracted(exDir, manifest, dpk::WideToUtf8(c.name));
        Check(fail == 0 && contentOk && result.entryCount == manifest.size(),
              std::string("自回环 ") + dpk::WideToUtf8(c.name) + "（条目 " + std::to_string(result.entryCount) + "，包大小 " +
                  std::to_string(result.packageSize) + "）");
    }

    // 模板包头
    dpk::PackOptions opt;
    std::string terr;
    if (dpk::LoadTemplateHeader(dpk::JoinPath(workDir, L"packages\\ref_flags3.dpk"), opt.templateHeader, terr)) {
        dpk::PackResult result;
        std::string perr;
        std::wstring outPath = dpk::JoinPath(outDir, L"pack_ce_template.dpk");
        if (!dpk::PackFolder(srcDir, outPath, opt, result, perr, dpk::ProgressCallback())) {
            Check(false, "模板包头打包失败: " + perr);
        } else {
            dpk::Reader reader;
            std::string oerr;
            bool ok = reader.Open(outPath, oerr) && reader.LoadEntries(oerr);
            Check(ok, "模板包头打包结果可读回");
        }
    } else {
        Check(false, "读取模板包头失败: " + terr);
    }
}

// ---- 损坏包与非法输入的读取侧边界判定：必须被拒绝，并给出原因 ----

uint32_t CorruptU32(const std::vector<uint8_t>& b, size_t off) {
    return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) | ((uint32_t)b[off + 2] << 16) |
           ((uint32_t)b[off + 3] << 24);
}

void CorruptPutU32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off] = (uint8_t)(v & 0xFF);
    b[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    b[off + 2] = (uint8_t)((v >> 16) & 0xFF);
    b[off + 3] = (uint8_t)((v >> 24) & 0xFF);
}

// 样本的目录树流为单块、未压缩存储：改写其明文后须同步刷新头部记录的 MD5，
// 才能真正触发目录树解析器的校验，而不是先被 whsc 的 MD5 拦住。
void CorruptRehashTree(std::vector<uint8_t>& b, size_t treeHead, uint32_t treeFileSize) {
    const size_t plainLen = (size_t)treeFileSize - 48;
    const dpk::Md5Digest d = dpk::Md5Compute(&b[treeHead + 48], plainLen);
    for (size_t i = 0; i < 16; ++i) {
        b[treeHead + 16 + i] = d[i];
    }
}

bool CorruptSave(const std::wstring& dir, const char* name, const std::vector<uint8_t>& bytes, std::wstring& out) {
    out = dpk::JoinPath(dir, dpk::Utf8ToWide(name));
    std::string err;
    return dpk::WriteFileBytes(out, bytes.empty() ? nullptr : &bytes[0], bytes.size(), err);
}

void ExpectOpenFail(const std::wstring& dir, const char* name, const std::vector<uint8_t>& bytes, const char* what) {
    std::wstring f;
    if (!CorruptSave(dir, name, bytes, f)) {
        Check(false, std::string("写入损坏包样本失败 ") + name);
        return;
    }
    dpk::Reader r;
    std::string err;
    bool opened = r.Open(f, err);
    Check(!opened, std::string(what) + " -> 打开阶段被拒绝（" + (opened ? std::string("竟被接受") : err) + "）");
}

void ExpectLoadFail(const std::wstring& dir, const char* name, const std::vector<uint8_t>& bytes, const char* what) {
    std::wstring f;
    if (!CorruptSave(dir, name, bytes, f)) {
        Check(false, std::string("写入损坏包样本失败 ") + name);
        return;
    }
    dpk::Reader r;
    std::string err;
    if (!r.Open(f, err)) {
        Check(false, std::string(what) + " -> 打开阶段意外失败（" + err + "）");
        return;
    }
    bool loaded = r.LoadEntries(err);
    Check(!loaded, std::string(what) + " -> 枚举阶段被拒绝（" + (loaded ? std::string("竟被接受") : err) + "）");
}

void ExpectEntryFail(const std::wstring& dir, const char* name, const std::vector<uint8_t>& bytes, const char* what) {
    std::wstring f;
    if (!CorruptSave(dir, name, bytes, f)) {
        Check(false, std::string("写入损坏包样本失败 ") + name);
        return;
    }
    dpk::Reader r;
    std::string err;
    if (!r.Open(f, err) || !r.LoadEntries(err)) {
        Check(false, std::string(what) + " -> 打开或枚举意外失败（" + err + "）");
        return;
    }
    if (r.Entries().empty()) {
        Check(false, std::string(what) + " -> 条目为空");
        return;
    }
    std::vector<uint8_t> plain;
    dpk::WhscInfo info;
    bool ok = r.ReadEntry(r.Entries()[0], plain, info, err);
    Check(!ok, std::string(what) + " -> 内容读取被拒绝（" + (ok ? std::string("竟被接受") : err) + "）");
}

void TestCorrupt(const std::wstring& workDir) {
    std::wstring src = dpk::JoinPath(workDir, L"packages\\ref_flags0.dpk");
    std::wstring dir = dpk::JoinPath(workDir, L"corrupt");
    std::string derr;
    if (!dpk::EnsureDirectory(dir, &derr)) {
        Check(false, "创建 corrupt 目录失败: " + derr);
        return;
    }
    std::vector<uint8_t> base;
    std::string rerr;
    if (!dpk::ReadFileBytes(src, base, rerr) || base.size() < 4096) {
        Check(false, "读取损坏包测试样本失败: " + rerr);
        return;
    }
    const uint32_t totalBlocks = (uint32_t)(base.size() / dpk::kBlockSize);
    const uint32_t treeRoot = CorruptU32(base, 0x84);
    const uint32_t treeFileSize = CorruptU32(base, (size_t)treeRoot * 1024 + 12);
    const size_t treeHead = (size_t)treeRoot * 1024 + 32;        // 目录树流的 whsc 头
    const size_t treeRecOff = treeHead + 48;                      // 目录树明文记录起点（样本为单块未压缩）

    // 1 魔数错误
    {
        std::vector<uint8_t> b = base;
        const char* magic = "whpackage2.0";
        memcpy(&b[0], magic, 12);
        ExpectOpenFail(dir, "bad_magic.dpk", b, "魔数错误");
    }
    // 2 长度不足 1024 字节
    {
        std::vector<uint8_t> b(base.begin(), base.begin() + 512);
        ExpectOpenFail(dir, "too_short.dpk", b, "长度不足 1024 字节");
    }
    // 3 包头块号越界
    {
        std::vector<uint8_t> b = base;
        CorruptPutU32(b, 0x84, totalBlocks + 8);
        ExpectOpenFail(dir, "root_range.dpk", b, "包头块号越界");
    }
    // 4 块链断裂：谎报虚拟文件长度，并让 next 指向 prev 不匹配的块
    {
        std::vector<uint8_t> b = base;
        CorruptPutU32(b, (size_t)treeRoot * 1024 + 12, 2000);
        CorruptPutU32(b, (size_t)treeRoot * 1024 + 4, 5);
        ExpectLoadFail(dir, "chain_break.dpk", b, "块链断裂（prev 不匹配）");
    }
    // 5 块链成环
    {
        std::vector<uint8_t> b = base;
        CorruptPutU32(b, (size_t)treeRoot * 1024 + 12, 2000);
        CorruptPutU32(b, (size_t)treeRoot * 1024 + 4, treeRoot);
        ExpectLoadFail(dir, "chain_cycle.dpk", b, "块链成环");
    }
    // 6 虚拟文件长度超出包容量
    {
        std::vector<uint8_t> b = base;
        CorruptPutU32(b, (size_t)treeRoot * 1024 + 12, 0xFFFFFFF0u);
        ExpectLoadFail(dir, "file_size_huge.dpk", b, "虚拟文件长度超出包容量");
    }
    // 7 目录树出现未知记录类型
    {
        std::vector<uint8_t> b = base;
        b[treeRecOff] = 0x09;
        CorruptRehashTree(b, treeHead, treeFileSize);
        ExpectLoadFail(dir, "tree_bad_tag.dpk", b, "目录树未知记录类型");
    }
    // 8 目录树引用的名称偏移不存在
    {
        std::vector<uint8_t> b = base;
        CorruptPutU32(b, treeRecOff + 1, 0x00FFFFF0u);
        CorruptRehashTree(b, treeHead, treeFileSize);
        ExpectLoadFail(dir, "tree_bad_nameoff.dpk", b, "目录树名称偏移越界");
    }
    // 9 目录树流的 whsc 魔数错误
    {
        std::vector<uint8_t> b = base;
        b[treeHead] = 'x';
        ExpectLoadFail(dir, "tree_bad_whsc.dpk", b, "目录树流魔数错误");
    }

    // 条目级：先按正常流程取出首条目的内容块
    dpk::Reader probe;
    std::string perr;
    uint32_t firstEntryRoot = 0;
    if (probe.Open(src, perr) && probe.LoadEntries(perr) && !probe.Entries().empty()) {
        firstEntryRoot = probe.Entries()[0].rootBlock;
    } else {
        Check(false, "定位首条目根块失败: " + perr);
        return;
    }
    const size_t entryHead = (size_t)firstEntryRoot * 1024 + 32;

    // 10 条目 MD5 不匹配
    {
        std::vector<uint8_t> b = base;
        b[entryHead + 16] = (uint8_t)(b[entryHead + 16] ^ 0xFF);
        ExpectEntryFail(dir, "entry_md5.dpk", b, "条目 MD5 不匹配");
    }
    // 11 条目明文长度不符
    {
        std::vector<uint8_t> b = base;
        CorruptPutU32(b, entryHead + 36, CorruptU32(b, entryHead + 36) + 1);
        ExpectEntryFail(dir, "entry_size.dpk", b, "条目明文长度不符");
    }
    // 12 条目根块号越界（枚举仍可用，内容读取必须失败）
    {
        std::vector<uint8_t> b = base;
        size_t p = treeRecOff;
        size_t tagOff = 0;
        const size_t treeEnd = treeRecOff + treeFileSize - 48;
        while (p < treeEnd) {
            const uint8_t tag = b[p];
            if (tag == 1) {
                tagOff = p;
                break;
            }
            if (tag == 2) {
                p += 5;
            } else if (tag == 3) {
                p += 1;
            } else {
                break;
            }
        }
        if (tagOff == 0) {
            Check(false, "目录树中未定位到文件记录（样本结构变化）");
        } else {
            CorruptPutU32(b, tagOff + 5, totalBlocks + 8);
            CorruptRehashTree(b, treeHead, treeFileSize);
            ExpectEntryFail(dir, "entry_block_range.dpk", b, "条目根块号越界");
        }
    }

    // 13 解包路径安全检查（拒绝绝对路径与 .. 逃逸）
    Check(!dpk::IsSafeRelativePath("..\\evil.txt") && !dpk::IsSafeRelativePath("/abs.txt") &&
              !dpk::IsSafeRelativePath("a/../../b.txt") && !dpk::IsSafeRelativePath("C:/win.txt") &&
              !dpk::IsSafeRelativePath("a//b.txt") && dpk::IsSafeRelativePath("a/b/c.txt") &&
              dpk::IsSafeRelativePath("数据/中文名.txt"),
          "解包路径安全检查（拒绝绝对路径与 .. 逃逸）");
}

}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2) {
        printf("用法: TestCore.exe <workdir>\n");
        return 2;
    }
    std::wstring workDir = dpk::Utf8ToWide(argv[1]);

    printf("=== DPKViewer 核心模块验证 ===\n工作目录: %s\n\n", argv[1]);

    Check(dpk::Md5SelfTest(), "MD5 标准向量自检");
    Check(dpk::Des::SelfTest(), "DES 标准向量自检");

    std::string detail;
    bool zlibOk = dpk::ZlibSelfTest(detail);  // 先调用再拼消息，避免参数求值顺序问题
    Check(zlibOk, "zlib 压缩/解压往返自检 (" + detail + ")");

    TestDesFixtures(workDir);
    TestZlibFixtures(workDir);

    std::vector<ManifestItem> manifest = LoadManifest(dpk::JoinPath(workDir, L"manifest.txt"));
    Check(!manifest.empty(), "载入 manifest.txt（" + std::to_string(manifest.size()) + " 条）");

    TestExtract(workDir, manifest);
    TestPack(workDir, manifest);
    TestCorrupt(workDir);

    printf("\n检查 %d 项，失败 %d 项\n", g_checks, g_failures);
    printf("%s\n", g_failures == 0 ? "CORE-TEST: PASS" : "CORE-TEST: FAIL");
    return g_failures == 0 ? 0 : 1;
}