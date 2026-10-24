// DpkTypes.h - 包条目与回调类型（不依赖 MFC）
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace dpk {

// 包内一个文件条目
struct Entry {
    std::string pathUtf8;   // 包内相对路径，'/' 分隔
    uint32_t rootBlock = 0; // 内容所在块链的根块号
    uint32_t blockCount = 0;// 块链长度（0 表示未统计）
};

// 进度回调：percent 0..100；返回 false 表示请求取消
typedef std::function<bool(const std::wstring& message, int percent)> ProgressCallback;

// 单条目结果回调：返回 false 表示请求取消
typedef std::function<bool(const std::string& pathUtf8, bool ok, const std::wstring& message)> EntryResultCallback;

}  // namespace dpk