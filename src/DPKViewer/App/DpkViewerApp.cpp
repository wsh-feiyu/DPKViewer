// DpkViewerApp.cpp - 应用对象实现
#include <afxwin.h>
#include <afxcmn.h>

#include "DpkViewerApp.h"
#include "Cli.h"
#include "MainFrame.h"
#include "resource.h"

#include <objbase.h>
#include <shellscalingapi.h>

CDPKViewerApp theApp;

CDPKViewerApp::CDPKViewerApp() {}

BOOL CDPKViewerApp::InitInstance() {
    // 在创建任何窗口前激活真正的每显示器 V2 DPI 感知，否则 manifest 里的声明只是"半吊子"
    // 这能让 Windows 走原生像素渲染管线，避免位图拉伸导致的边缘模糊
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 列表视图、进度条等通用控件需要在创建控件前注册
    INITCOMMONCONTROLSEX icc = {0};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    ::InitCommonControlsEx(&icc);

    // Vista 风格的文件/文件夹对话框需要 COM
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // 命令行模式（/list、/extract、/pack）在创建窗口前分流，不弹任何界面
    const int cli = app::RunCli(std::wstring(m_lpCmdLine));
    if (cli >= 0) {
        cliExit_ = cli;
        return FALSE;
    }

    if (!CWinApp::InitInstance()) {
        return FALSE;
    }

    CMainFrame* frame = new CMainFrame;
    if (!frame->Create(nullptr, L"DPKViewer - 寻仙 DPK 包浏览 / 解包 / 生成工具", WS_OVERLAPPEDWINDOW,
                       CRect(0, 0, 1180, 760), nullptr, MAKEINTRESOURCE(IDR_MAINFRAME))) {
        delete frame;
        return FALSE;
    }
    m_pMainWnd = frame;

    frame->ShowWindow(m_nCmdShow);
    frame->UpdateWindow();
    return TRUE;
}

int CDPKViewerApp::ExitInstance() {
    ::CoUninitialize();
    return cliExit_ != 0 ? cliExit_ : CWinApp::ExitInstance();
}