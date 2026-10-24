// DpkViewerApp.h - 应用对象：启动分流（命令行模式 / 主窗口）
#pragma once

#include <afxwin.h>

class CDPKViewerApp : public CWinApp {
public:
    CDPKViewerApp();

    virtual BOOL InitInstance();
    virtual int ExitInstance();

private:
    int cliExit_ = 0;
};