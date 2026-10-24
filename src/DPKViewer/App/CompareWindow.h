// CompareWindow.h - DPK 包对比独立窗口（工具菜单 / ID_APP_COMPARE 打开）
#pragma once

#include <string>
#include <vector>

#include <afxwin.h>
#include <afxcmn.h>

#include "Core/DpkCompare.h"

// 对比窗口销毁后通知主窗口清空指针
const UINT kWMCompareClosed = WM_APP + 101;

class CCompareWindow : public CFrameWnd {
    DECLARE_DYNAMIC(CCompareWindow)

public:
    CCompareWindow();
    virtual ~CCompareWindow();
    BOOL Create(CWnd* pParent);

protected:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
    afx_msg void OnClose();
    afx_msg void OnBrowseA();
    afx_msg void OnBrowseB();
    afx_msg void OnRun();
    afx_msg void OnExport();
    afx_msg void OnFilterChanged();
    afx_msg void OnEntryChanged(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnEntryDblClk(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);
    virtual void PostNcDestroy();
    DECLARE_MESSAGE_MAP()

private:
    void Layout();
    void ApplyFontToChildren();
    void RebuildView();
    void ShowDetail(int row);
    void SetStatus(const CString& text);
    bool ProgressTick(const std::wstring& message, int percent);
    // 解析「大小差异上限」：-1 不限制；>=0 为 MB 上限；解析失败填 errText 返回 -2
    int ParseSizeLimit(CString& errText);

    CStatic m_lblA;
    CEdit m_edtA;
    CButton m_btnBrowseA;
    CStatic m_lblB;
    CEdit m_edtB;
    CButton m_btnBrowseB;
    CStatic m_lblDepth;
    CComboBox m_cmbDepth;
    CStatic m_lblLimit;
    CComboBox m_cmbLimit;
    CButton m_btnRun;
    CButton m_btnExport;
    CButton m_btnClose;
    CStatic m_lblFilter;
    CComboBox m_cmbFilter;
    CStatic m_lblStats;
    CListCtrl m_list;
    CEdit m_detail;
    CStatic m_lblPkgInfo;
    CProgressCtrl m_progress;
    CStatic m_status;

    CFont m_fontMain;

    dpk::CompareResult m_result;
    dpk::CompareDepth m_depth = dpk::CompareDepth::Metadata;
    std::wstring m_pathA;  // 参与对比的包路径（双击查看内容对比时复用）
    std::wstring m_pathB;
    std::vector<int> m_view;   // 列表行 -> m_result.entries 下标
    bool m_hasResult = false;
    bool m_ignoreSel = false;
    bool m_busy = false;
    bool m_cancel = false;
};
