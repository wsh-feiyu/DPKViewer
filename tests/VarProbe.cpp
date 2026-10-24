// VarProbe.cpp - 逐行复算 des_fixtures.txt 中的 VAR 变体向量（临时探针，不参与交付）
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Core/Des.h"
#include "Core/FileIO.h"
#include "Core/StrUtil.h"

static std::vector<uint8_t> HexToBytes(const std::string& s) {
    std::vector<uint8_t> out;
    auto hv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    for (size_t i = 0; i + 1 < s.size(); i += 2) out.push_back((uint8_t)((hv(s[i]) << 4) | hv(s[i + 1])));
    return out;
}

static std::string ToHex(const std::vector<uint8_t>& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < b.size(); ++i) {
        s.push_back(d[b[i] >> 4]);
        s.push_back(d[b[i] & 0xF]);
    }
    return s;
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::vector<uint8_t> raw;
    std::string err;
    if (!dpk::ReadFileBytes(dpk::Utf8ToWide(argv[1]), raw, err)) {
        printf("read fail: %s\n", err.c_str());
        return 1;
    }
    std::string text(raw.begin(), raw.end());
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (end == text.size() && line.empty()) break;
        pos = end + 1;
        std::vector<std::string> p;
        size_t s = 0;
        while (s < line.size()) {
            while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) ++s;
            size_t e = s;
            while (e < line.size() && line[e] != ' ' && line[e] != '\t') ++e;
            if (e > s) p.push_back(line.substr(s, e - s));
            s = e;
        }
        if (p.size() != 5 || p[0] != "VAR") continue;
        std::vector<uint8_t> key = HexToBytes(p[1]);
        std::vector<uint8_t> plain = HexToBytes(p[2]);
        std::vector<uint8_t> expect = HexToBytes(p[3]);
        uint8_t key8[8];
        memset(key8, 0, sizeof(key8));
        memcpy(key8, &key[0], key.size() < 8 ? key.size() : 8);
        std::vector<uint8_t> got = dpk::DesCryptPayload(plain, key8, false);
        std::vector<uint8_t> back = dpk::DesCryptPayload(expect, key8, true);
        printf("VAR len=%-4d key=%s plain=%s cpp=%s exp=%s back=%s selfback=%s %s\n", (int)plain.size(), p[1].c_str(),
               p[2].c_str(), ToHex(got).c_str(), p[3].c_str(), ToHex(back).c_str(),
               ToHex(dpk::DesCryptPayload(got, key8, true)).c_str(),
               (ToHex(got) == p[3]) ? "OK" : "MISMATCH");
    }
    return 0;
}