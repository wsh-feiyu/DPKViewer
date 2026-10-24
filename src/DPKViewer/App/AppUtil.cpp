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

void AttachParentConsole() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;  // 没有父控制台（例如从资源管理器启动）时保持无输出
    }
    SetConsoleOutputCP(CP_UTF8);  // 控制台按 UTF-8 输出，保证中文路径可读
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    freopen_s(&f, "CONIN$", "r", stdin);
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
    printf("%s\n", utf8Line.c_str());
    fflush(stdout);
}

void Reporter::Line(const std::string& text) {
    write(text);
}

void Reporter::LineW(const std::wstring& text) {
    write(utf8Of(text));
}

}  // namespace app