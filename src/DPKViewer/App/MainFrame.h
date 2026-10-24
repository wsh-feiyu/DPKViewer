// MainFrame.h - DPKViewer 主窗口（浏览 / 解包 / 生成）
#pragma once

#include <vector>

#include <afxwin.h>
#include <afxcmn.h>

#include "Core/DpkReader.h"
#include "Core/DpkTypes.h"
#include "Core/Whsc.h"

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
    afx_msg void OnUsage();
    afx_msg void OnAbout();
    afx_msg void OnAppExit();
    afx_msg void OnFilterChanged();
    afx_msg void OnEntryChanged(NMHDR* pNMHDR, LRESULT* pResult);
    DECLARE_MESSAGE_MAP()

private:
    struct EntryMeta {
        dpk::WhscInfo info;
        bool headerOk = false;
        std::string err;
    };

    void Layout();
    void RefreshList();
    void ShowDetail(int row);
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
    CListCtrl m_list;
    CEdit m_detail;
    CProgressCtrl m_progress;
    CStatic m_status;

    CFont m_fontMain;           // 统一的主字体：Microsoft YaHei UI / Segoe UI，按 DPI 计算像素高度

    dpk::Reader m_reader;
    std::vector<int> m_view;          // 列表行 -> 条目下标
    std::vector<EntryMeta> m_meta;    // 条目下标 -> whsc 头信息
    bool m_ignoreSel = false;
    bool m_busy = false;
    bool m_cancel = false;
};