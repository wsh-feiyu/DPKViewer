// AppUtil.h - 界面层通用小工具（文件夹选择、控制台附加、字节格式化）
#pragma once

#include <windows.h>

#include <cstdio>
#include <string>

namespace app {

// 命令行模式下把当前进程的输出接回父控制台（GUI 子系统的 exe 默认没有控制台）
void AttachParentConsole();

// /quiet 时关闭控制台输出（日志仍写）
void SetConsoleOutputEnabled(bool enabled);

// 选择文件夹（Vista 风格 IFileOpenDialog，FOS_PICKFOLDERS）
bool PickFolder(HWND owner, const wchar_t* title, std::wstring& out);

// 打印一行到 stdout 与（可选的）日志文件
class Reporter {
public:
    explicit Reporter(const std::wstring& logPath = std::wstring());
    ~Reporter();

    void Line(const std::string& text);        // UTF-8 文本（接受 const char*）
    void LineW(const std::wstring& text);      // 宽字符文本
    void Open(const std::wstring& logPath);    // 运行中改用新的日志文件
    bool Failed() const { return failed_; }
    void MarkFailed() { failed_ = true; }

private:
    std::string utf8Of(const std::wstring& text) const;
    void write(const std::string& utf8Line);
    FILE* log_ = nullptr;
    bool failed_ = false;
    bool console_ = false;
};

}  // namespace app