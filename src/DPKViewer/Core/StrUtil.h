// StrUtil.h - 字符串 / 路径工具（不依赖 MFC）
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dpk {

// UTF-8 <-> 宽字符
std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);

// 包内名称字节 -> 宽字符：先按 UTF-8 严格解码，失败则按系统 ANSI 代码页
std::wstring NameBytesToWide(const std::string& bytes);
// 检测文本编码：0 空 / 1 UTF-8 / 2 ANSI（中文系统即 GBK）/ 3 UTF-8 带 BOM
int DetectTextEncoding(const std::string& bytes);
// 宽字符 -> 包内名称字节：优先系统 ANSI 代码页，无法表示时使用 UTF-8
std::string WideToNameBytes(const std::wstring& s);

// 路径工具
std::wstring JoinPath(const std::wstring& dir, const std::wstring& name);
std::string NormalizeSlashes(const std::string& s);          // '\\' -> '/'
std::wstring NormalizeSlashesW(const std::wstring& s);       // '\\' -> '/'
std::wstring RepoRelativeToWin(const std::wstring& rel);     // '/' -> '\\'
std::wstring FileNameOf(const std::wstring& path);
std::wstring DirectoryOf(const std::wstring& path);
bool EnsureDirectory(const std::wstring& dir, std::string* err = nullptr);

// 相对路径安全性：拒绝绝对路径与 ".." 分量
bool IsSafeRelativePath(const std::string& relPathUtf8);

std::string ToLowerAscii(std::string s);

// 十六进制与文本预览
std::string HexDump(const uint8_t* data, size_t len, size_t maxBytes);
bool LooksLikeText(const uint8_t* data, size_t len);

// 数值格式化
std::string FormatBytes(uint64_t n);
std::string FormatUInt(uint64_t n);

}  // namespace dpk