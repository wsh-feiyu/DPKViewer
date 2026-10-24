// FileIO.cpp
#include "FileIO.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>

#include "StrUtil.h"

namespace dpk {

namespace fs = std::filesystem;

bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out, std::string& err) {
    out.clear();
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || fp == nullptr) {
        err = "无法打开文件";
        return false;
    }
    _fseeki64(fp, 0, SEEK_END);
    __int64 size = _ftelli64(fp);
    if (size < 0) {
        fclose(fp);
        err = "无法读取文件大小";
        return false;
    }
    _fseeki64(fp, 0, SEEK_SET);
    out.resize((size_t)size);
    size_t read = size ? fread(&out[0], 1, (size_t)size, fp) : 0;
    fclose(fp);
    if (read != (size_t)size) {
        out.clear();
        err = "读取文件不完整";
        return false;
    }
    return true;
}

bool WriteFileBytes(const std::wstring& path, const uint8_t* data, size_t len, std::string& err) {
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"wb") != 0 || fp == nullptr) {
        err = "无法创建文件";
        return false;
    }
    size_t written = len ? fwrite(data, 1, len, fp) : 0;
    int flushResult = fflush(fp);
    fclose(fp);
    if (written != len || flushResult != 0) {
        err = "写入文件不完整";
        return false;
    }
    return true;
}

bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool GetFileSize(const std::wstring& path, uint64_t& size) {
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) return false;
    size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    return true;
}

bool ListFilesRecursive(const std::wstring& rootDir, std::vector<std::wstring>& outFiles, std::string& err) {
    outFiles.clear();
    std::error_code ec;
    fs::path root(rootDir);
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        err = "目录不存在";
        return false;
    }
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        err = "无法枚举目录: " + ec.message();
        return false;
    }
    fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            err = "枚举目录出错: " + ec.message();
            return false;
        }
        std::error_code entryEc;
        if (it->is_symlink(entryEc)) {
            if (it->is_directory(entryEc)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(entryEc)) outFiles.push_back(it->path().wstring());
    }
    std::sort(outFiles.begin(), outFiles.end());
    return true;
}

std::wstring MakeRelativeSlashPath(const std::wstring& rootDir, const std::wstring& fullPath) {
    std::wstring root = NormalizeSlashesW(rootDir);
    while (root.size() > 1 && root[root.size() - 1] == L'/') root.erase(root.size() - 1);
    std::wstring full = NormalizeSlashesW(fullPath);
    if (!root.empty() && full.size() > root.size() + 1 && full.compare(0, root.size() + 1, root + L"/") == 0) {
        return full.substr(root.size() + 1);
    }
    return full;
}

}  // namespace dpk