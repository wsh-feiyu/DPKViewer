// MainFrame.h - DPKViewer 主窗口（浏览 / 解包 / 生成）
#pragma once

#include <vector>

#include <afxwin.h>
#include <afxcmn.h>

#include "Core/DpkReader.h"
#include "Core/DpkTypes.h"
#include "Core/Whsc.h"

class CCompareWindow;

class CMainFrame : public CFrameWnd {
    DECLARE_DYNAMIC(CMainFrame)

public:
    CMainFrame();
    virtual ~CMainFrame();

protected:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
    afx_msg void OnOpenPackage();
    afx_msg void OnExtractAll();
    afx_msg void OnPackFolder();
    afx_msg void OnComparePackages();
    afx_msg void OnUsage();
    afx_msg void OnAbout();
    afx_msg void OnAppExit();
    afx_msg void OnFilterChanged();
    afx_msg void OnFolderViewChanged();
    afx_msg void OnEntryChanged(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnListDblClk(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnListRightClick(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnCtxViewFile();
    afx_msg void OnCtxExportFile();
    afx_msg void OnCtxExportFolder();
    afx_msg void OnCtxImportFile();
    afx_msg void OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg LRESULT OnCompareClosed(WPARAM wParam, LPARAM lParam);
    DECLARE_MESSAGE_MAP()

private:
    struct EntryMeta {
        dpk::WhscInfo info;
        bool headerOk = false;
        std::string err;
        // 校验结果：-1 未校验 / 0 校验失败 / 1 校验通过
        int verifyState = -1;
    };

    // 列表行：entryIdx >= 0 为条目行；entryIdx < 0 且 isParent 为「..」，否则为文件夹行
    struct ListRow {
        int entryIdx = -1;
        bool isParent = false;
        std::wstring name;
    };

    void Layout();
    void RefreshList();
    void ShowDetail(int row);
    void ViewEntryAtRow(int row);  // 解码条目并打开查看窗口
    void SetStatus(const CString& text);
    bool OpenPackage(const std::wstring& path);
    bool ProgressTick(const std::wstring& message, int percent);

    CButton m_btnOpen;
    CButton m_btnExtract;
    CButton m_btnPack;
    CButton m_btnUsage;
    CButton m_btnAbout;
    CStatic m_lblFilter;
    CEdit m_edtFilter;
    CButton m_chkCompress;
    CButton m_chkEncrypt;
    CButton m_chkTemplate;
    CButton m_chkFolder;
    CListCtrl m_list;
    CEdit m_detail;
    CProgressCtrl m_progress;
    CStatic m_status;

    CFont m_fontMain;           // 统一的主字体：Microsoft YaHei UI / Segoe UI，按 DPI 计算像素高度

    dpk::Reader m_reader;
    std::vector<ListRow> m_view;      // 列表行（平铺模式全部为条目行）
    std::vector<EntryMeta> m_meta;    // 条目下标 -> whsc 头信息
    bool m_folderMode = true;         // 文件夹收纳视图（默认开）
    std::wstring m_curFolder;         // 当前文件夹（L"" = 根，'/' 分隔，无尾斜杠）
    CCompareWindow* m_compareWnd = nullptr;  // 工具：DPK 对比窗口（自销毁，关闭时通知清空）
    bool m_ignoreSel = false;
    bool m_busy = false;
    bool m_cancel = false;
    int m_ctxRow = -1;  // 右键菜单对应的列表行
};