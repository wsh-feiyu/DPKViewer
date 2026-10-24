// TreeProbe.cpp - 对比两个包的目录树/路径表明文（定位模板复用失败原因）
// 编译：cl /utf-8 TreeProbe.cpp DpkReader.cpp Whsc.cpp Des.cpp Md5.cpp Zlib.cpp StrUtil.cpp FileIO.cpp
#include <cstdio>
#include <string>
#include <vector>

#include "../src/DPKViewer/Core/DpkReader.h"
#include "../src/DPKViewer/Core/Whsc.h"

static bool DumpStream(const wchar_t* path, uint32_t root, std::vector<uint8_t>& plain) {
    dpk::Reader r;
    std::string err;
    if (!r.Open(path, err)) {
        printf("open fail: %s\n", err.c_str());
        return false;
    }
    std::vector<uint8_t> stream;
    if (!r.ReadVirtualFile(root, stream, err)) {
        printf("chain fail: %s\n", err.c_str());
        return false;
    }
    dpk::WhscInfo info;
    if (!dpk::WhscDecode(stream, plain, info, err)) {
        printf("decode fail: %s\n", err.c_str());
        return false;
    }
    return true;
}

static void Report(const char* tag, const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    printf("[%s] A=%zu B=%zu bytes\n", tag, a.size(), b.size());
    if (a == b) {
        printf("  IDENTICAL\n");
        return;
    }
    size_t diff = (size_t)-1;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        if (a[i] != b[i]) {
            diff = i;
            break;
        }
    }
    printf("  first diff at %zu\n", diff == (size_t)-1 ? (a.size() < b.size() ? a.size() : b.size()) : diff);
    const size_t s = diff < 16 ? 0 : diff - 16;
    const size_t e = s + 48;
    for (size_t i = s; i < e; ++i) {
        if (i >= a.size() && i >= b.size()) break;
        printf("  %06zu  %02x  %02x%s\n", i, i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0,
               (i < a.size() && i < b.size() && a[i] != b[i]) ? "  <--" : "");
    }
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        printf("usage: TreeProbe <pkgA> <pkgB>\n");
        return 1;
    }
    std::vector<uint8_t> ta, tb, na, nb;
    dpk::Reader ra, rb;
    std::string err;
    if (!ra.Open(argv[1], err) || !ra.LoadEntries(err, false)) return 1;
    if (!rb.Open(argv[2], err) || !rb.LoadEntries(err, false)) return 1;
    if (!DumpStream(argv[1], ra.TreeRoot(), ta)) return 1;
    if (!DumpStream(argv[2], rb.TreeRoot(), tb)) return 1;
    if (!DumpStream(argv[1], ra.NamesRoot(), na)) return 1;
    if (!DumpStream(argv[2], rb.NamesRoot(), nb)) return 1;
    Report("tree", ta, tb);
    Report("names", na, nb);
    return 0;
}
