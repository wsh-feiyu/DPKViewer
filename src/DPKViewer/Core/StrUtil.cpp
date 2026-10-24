// StrUtil.cpp
#include "StrUtil.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <cstring>

namespace dpk {

namespace {

bool IsValidUtf8(const std::string& s) {
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t extra = 0;
        if (c < 0x80) {
            ++i;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            extra = 1;
            if (c < 0xC2) return false;  // 过长编码 / 非法
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            if (c > 0xF4) return false;
        } else {
            return false;
        }
        if (i + extra >= n) return false;
        for (size_t k = 1; k <= extra; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

}  // namespace

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), (int)s.size(), nullptr, 0);
    if (need <= 0) {
        // 非法 UTF-8：按 ANSI 处理，保证不丢内容
        need = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0);
        if (need <= 0) return std::wstring();
        std::wstring out((size_t)need, L'\0');
        MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &out[0], need);
        return out;
    }
    std::wstring out((size_t)need, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), (int)s.size(), &out[0], need);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    int need = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (need <= 0) return std::string();
    std::string out((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], need, nullptr, nullptr);
    return out;
}

int DetectTextEncoding(const std::string& bytes) {
    if (bytes.empty()) return 0;
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB &&
        (unsigned char)bytes[2] == 0xBF) {
        return 3;  // UTF-8 BOM
    }
    if (IsValidUtf8(bytes)) {
        const int need =
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.c_str(), (int)bytes.size(), nullptr, 0);
        if (need > 0) return 1;  // UTF-8（无 BOM）
    }
    return 2;  // 非 UTF-8：按系统 ANSI 代码页解码（中文系统即 GBK）
}

std::wstring NameBytesToWide(const std::string& bytes) {
    if (bytes.empty()) return std::wstring();
    if (IsValidUtf8(bytes)) {
        int need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.c_str(), (int)bytes.size(), nullptr, 0);
        if (need > 0) {
            std::wstring out((size_t)need, L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.c_str(), (int)bytes.size(), &out[0], need);
            return out;
        }
    }
    int need = MultiByteToWideChar(CP_ACP, 0, bytes.c_str(), (int)bytes.size(), nullptr, 0);
    if (need <= 0) return Utf8ToWide(bytes);
    std::wstring out((size_t)need, L'\0');
    MultiByteToWideChar(CP_ACP, 0, bytes.c_str(), (int)bytes.size(), &out[0], need);
    return out;
}

std::string WideToNameBytes(const std::wstring& s) {
    if (s.empty()) return std::string();
    // 优先系统 ANSI 代码页（与原包中常见的中文名称编码一致）
    BOOL used = FALSE;
    int need = WideCharToMultiByte(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, &used);
    if (need > 0 && !used) {
        std::string out((size_t)need, '\0');
        WideCharToMultiByte(CP_ACP, 0, s.c_str(), (int)s.size(), &out[0], need, nullptr, nullptr);
        return out;
    }
    return WideToUtf8(s);
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    wchar_t last = dir[dir.size() - 1];
    if (last == L'\\' || last == L'/') return dir + name;
    return dir + L"\\" + name;
}

std::string NormalizeSlashes(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == '\\') out[i] = '/';
    }
    return out;
}

std::wstring NormalizeSlashesW(const std::wstring& s) {
    std::wstring out = s;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == L'\\') out[i] = L'/';
    }
    return out;
}

std::wstring RepoRelativeToWin(const std::wstring& rel) {
    std::wstring out = rel;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == L'/') out[i] = L'\\';
    }
    return out;
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return path;
    return path.substr(pos + 1);
}

std::wstring DirectoryOf(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return std::wstring();
    return path.substr(0, pos);
}

bool EnsureDirectory(const std::wstring& dir, std::string* err) {
    if (dir.empty()) return true;
    DWORD attr = GetFileAttributesW(dir.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) {
        if (attr & FILE_ATTRIBUTE_DIRECTORY) return true;
        if (err) *err = "路径已存在且不是目录";
        return false;
    }
    // 逐级创建
    std::wstring cur;
    size_t i = 0;
    if (dir.size() >= 2 && dir[1] == L':') {
        cur = dir.substr(0, 2);
        i = 2;
    }
    for (; i <= dir.size(); ++i) {
        if (i == dir.size() || dir[i] == L'\\' || dir[i] == L'/') {
            if (!cur.empty() && cur.back() != L':') {
                if (!CreateDirectoryW(cur.c_str(), nullptr)) {
                    DWORD e = GetLastError();
                    if (e != ERROR_ALREADY_EXISTS) {
                        if (err) *err = "创建目录失败";
                        return false;
                    }
                }
            }
        }
        if (i < dir.size()) cur.push_back(dir[i]);
    }
    return true;
}

bool IsSafeRelativePath(const std::string& relPathUtf8) {
    if (relPathUtf8.empty()) return false;
    std::string p = NormalizeSlashes(relPathUtf8);
    if (p[0] == '/' || p[0] == '\\') return false;
    if (p.size() >= 2 && p[1] == ':') return false;  // 盘符
    size_t start = 0;
    while (start <= p.size()) {
        size_t end = p.find('/', start);
        if (end == std::string::npos) end = p.size();
        std::string comp = p.substr(start, end - start);
        if (comp.empty() || comp == "." || comp == "..") return false;
        if (end == p.size()) break;
        start = end + 1;
    }
    return true;
}

std::string ToLowerAscii(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] - 'A' + 'a');
    }
    return s;
}

std::string HexDump(const uint8_t* data, size_t len, size_t maxBytes) {
    std::string out;
    char line[128];
    size_t n = len < maxBytes ? len : maxBytes;
    for (size_t i = 0; i < n; i += 16) {
        _snprintf_s(line, sizeof(line), _TRUNCATE, "%08zX  ", i);
        out += line;
        std::string ascii;
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < n) {
                unsigned char c = data[i + j];
                _snprintf_s(line, sizeof(line), _TRUNCATE, "%02X ", c);
                out += line;
                ascii.push_back((c >= 0x20 && c < 0x7F) ? (char)c : '.');
            } else {
                out += "   ";
                ascii.push_back(' ');
            }
            if (j == 7) out += " ";
        }
        out += " |";
        out += ascii;
        out += "|\r\n";
    }
    if (len > n) {
        _snprintf_s(line, sizeof(line), _TRUNCATE, "... 共 %zu 字节，仅显示前 %zu 字节\r\n", len, n);
        out += line;
    }
    return out;
}

bool LooksLikeText(const uint8_t* data, size_t len) {
    if (len == 0) return true;
    size_t sample = len < 4096 ? len : 4096;
    size_t bad = 0;
    for (size_t i = 0; i < sample; ++i) {
        unsigned char c = data[i];
        if (c == 0) return false;
        if (c < 0x09 || (c > 0x0D && c < 0x20)) ++bad;
    }
    return bad * 100 < sample * 2;  // 控制字符少于 2%
}

std::string FormatBytes(uint64_t n) {
    char buf[64];
    if (n < 1024) {
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%llu B", (unsigned long long)n);
    } else if (n < 1024ull * 1024) {
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.2f KB", (double)n / 1024.0);
    } else if (n < 1024ull * 1024 * 1024) {
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.2f MB", (double)n / (1024.0 * 1024.0));
    } else {
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.2f GB", (double)n / (1024.0 * 1024.0 * 1024.0));
    }
    return std::string(buf);
}

std::string FormatUInt(uint64_t n) {
    char buf[32];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%llu", (unsigned long long)n);
    return std::string(buf);
}

}  // namespace dpk