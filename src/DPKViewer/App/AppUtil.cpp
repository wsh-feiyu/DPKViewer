// AppUtil.cpp - 界面层通用小工具实现
#include "AppUtil.h"

#include <cstdio>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>

#include "Core/StrUtil.h"

#pragma comment(lib, "ole32.lib")

namespace app {

namespace {
// /quiet 时关闭控制台输出（WriteConsole 绕过 stdio，freopen NUL 拦不住）
bool g_consoleOut = true;
}  // namespace

void SetConsoleOutputEnabled(bool enabled) {
    g_consoleOut = enabled;
}

void AttachParentConsole() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;  // 没有父控制台（例如从资源管理器启动）时保持无输出
    }
    SetConsoleOutputCP(CP_UTF8);  // 控制台按 UTF-8 输出，保证中文路径可读
    // 两种情况要区分：
    //  - 父进程给了有效的标准输出句柄（> 文件重定向 / | 管道，脚本与 AI 助手都属此类）：
    //    必须保留原句柄，输出写入重定向目标。之前的实现无条件重接 CONOUT$，会把输出
    //    抢到（可能隐藏的）控制台里，导致管道/重定向拿到空内容
    //  - 直接在控制台里启动（GUI 子系统进程默认拿不到有效句柄）：接到 CONOUT$
    const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    const bool hasRedirect =
        hOut != nullptr && hOut != INVALID_HANDLE_VALUE && GetFileType(hOut) != FILE_TYPE_UNKNOWN;
    if (!hasRedirect) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        freopen_s(&f, "CONIN$", "r", stdin);
    }
}

bool PickFolder(HWND owner, const wchar_t* title, std::wstring& out) {
    IFileOpenDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || dlg == nullptr) {
        return false;
    }
    DWORD options = 0;
    if (SUCCEEDED(dlg->GetOptions(&options))) {
        dlg->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    if (title != nullptr) {
        dlg->SetTitle(title);
    }
    bool ok = false;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item != nullptr) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr) {
                out.assign(path);
                ok = !out.empty();
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
}

Reporter::Reporter(const std::wstring& logPath) {
    Open(logPath);
}

Reporter::~Reporter() {
    if (log_ != nullptr) {
        fclose(log_);
        log_ = nullptr;
    }
}

void Reporter::Open(const std::wstring& logPath) {
    if (log_ != nullptr) {
        fclose(log_);
        log_ = nullptr;
    }
    if (!logPath.empty()) {
        _wfopen_s(&log_, logPath.c_str(), L"wb");
        if (log_ != nullptr) {
            const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
            fwrite(bom, 1, 3, log_);
        }
    }
}

std::string Reporter::utf8Of(const std::wstring& text) const {
    return dpk::WideToUtf8(text);
}

void Reporter::write(const std::string& utf8Line) {
    if (log_ != nullptr) {
        // 日志以二进制方式打开，需自行补行尾；控制台由 printf 的文本模式负责换行
        static const char kCrLf[2] = {'\r', '\n'};
        fwrite(utf8Line.data(), 1, utf8Line.size(), log_);
        fwrite(kCrLf, 1, 2, log_);
        fflush(log_);
    }
    if (!g_consoleOut) {
        return;  // /quiet：只写日志，不写控制台
    }
    // 输出通道三态（首次调用时判定一次）：
    //  - 有控制台（交互式 cmd / PowerShell / Windows Terminal）：直接 WriteConsoleW 写 CONOUT$，
    //    与控制台程序内部机制一致，对 ConPTY 最兼容（GUI 子系统进程经 CRT 的 printf
    //    在部分控制台宿主下会丢输出）
    //  - 无控制台（重定向/管道，含 AI 助手与脚本调用）：printf 写入重定向目标
    static int channel = -1;
    static HANDLE hCon = nullptr;
    if (channel < 0) {
        const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        const bool valid = hOut != nullptr && hOut != INVALID_HANDLE_VALUE;
        const DWORD ft = valid ? GetFileType(hOut) : FILE_TYPE_UNKNOWN;
        if (valid && (ft == FILE_TYPE_PIPE || ft == FILE_TYPE_DISK)) {
            channel = 0;  // 管道/文件重定向：printf 直写目标（脚本与 AI 助手靠这个）
        } else if (GetConsoleWindow() != nullptr) {
            hCon = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
            channel = (hCon != INVALID_HANDLE_VALUE) ? 1 : 0;  // 交互控制台：WriteConsoleW
        } else {
            channel = 0;  // NUL（/quiet 已提前拦截）/ 无句柄
        }
    }
    if (channel == 1) {
        // WriteConsoleW 不做换行翻译，必须显式 \r\n；裸 \n 会让光标只下移不回行首
        const std::wstring w = dpk::Utf8ToWide(utf8Line) + L"\r\n";
        DWORD written = 0;
        WriteConsoleW(hCon, w.c_str(), (DWORD)w.size(), &written, nullptr);
    } else {
        printf("%s\n", utf8Line.c_str());
        fflush(stdout);
    }
}

void Reporter::Line(const std::string& text) {
    write(text);
}

void Reporter::LineW(const std::wstring& text) {
    write(utf8Of(text));
}

}  // namespace app