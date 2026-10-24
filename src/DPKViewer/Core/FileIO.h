// FileIO.h - 文件读写与目录遍历工具（不依赖 MFC）
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dpk {

bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out, std::string& err);
bool WriteFileBytes(const std::wstring& path, const uint8_t* data, size_t len, std::string& err);
bool FileExists(const std::wstring& path);
bool GetFileSize(const std::wstring& path, uint64_t& size);

// 递归列出目录下所有普通文件（返回完整路径，按路径排序）
bool ListFilesRecursive(const std::wstring& rootDir, std::vector<std::wstring>& outFiles, std::string& err);

// 去掉前缀目录，返回以 '/' 分隔的相对路径（宽字符）
std::wstring MakeRelativeSlashPath(const std::wstring& rootDir, const std::wstring& fullPath);

}  // namespace dpk