// PatchProbe.cpp - PatchEntryInPackage 原位补丁自检
// 编译：cl /std:c++17 /utf-8 /Fo:build\ /Fe:build\PatchProbe.exe tests\PatchProbe.cpp + Core 各 cpp
#include <cstdio>
#include <string>
#include <vector>

#include "../src/DPKViewer/Core/DpkWriter.h"
#include "../src/DPKViewer/Core/FileIO.h"

static int Run(const wchar_t* pkg, const char* rel, const std::vector<uint8_t>& data) {
    uint32_t entries = 0, blocks = 0;
    bool replaced = false;
    std::string err;
    const bool ok = dpk::PatchEntryInPackage(pkg, rel, data, true, true, 9, entries, blocks, replaced, err);
    printf("%s %s: entries=%u blocks=%u err=%s\n", ok ? "OK  " : "FAIL", replaced ? "replace" : "add", entries,
           blocks, err.c_str());
    return ok ? 0 : 1;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        printf("usage: PatchProbe <pkg-copy>\n");
        return 1;
    }
    const std::wstring pkg = argv[1];
    int rc = 0;

    // 场景1：替换小文件（内容变化，尺寸相近）
    std::string s1 = "patch probe replace v1\r\npatch probe replace v1\r\n";
    rc |= Run(pkg.c_str(), "help_bank/bank_spirit.txt", std::vector<uint8_t>(s1.begin(), s1.end()));

    // 场景2：替换为大内容（链扩展，~300KB）
    std::vector<uint8_t> big(300 * 1024);
    for (size_t i = 0; i < big.size(); ++i) big[i] = (uint8_t)(i * 31 + (i / 1024));
    rc |= Run(pkg.c_str(), "help_bank/bank_spirit.txt", big);

    // 场景3：新增条目（目录树/路径表重建）
    std::string s3 = "new entry from patch probe";
    rc |= Run(pkg.c_str(), "web/patch_probe_new.txt", std::vector<uint8_t>(s3.begin(), s3.end()));

    // 场景4：再次替换新增的条目（树不变路径）
    std::string s4 = "new entry from patch probe - v2";
    rc |= Run(pkg.c_str(), "web/patch_probe_new.txt", std::vector<uint8_t>(s4.begin(), s4.end()));
    return rc;
}
