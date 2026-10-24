// DiffViewer.h - 条目内容对比查看窗口（文本行级 diff / 十六进制逐字节比对）
#pragma once

#include <string>
#include <vector>

#include <afxwin.h>
#include <afxcmn.h>

// (COLORREF)-1 表示使用默认背景色
inline constexpr COLORREF kDiffBgSame = (COLORREF)-1;

class CDiffViewer : public CFrameWnd {
    DECLARE_DYNAMIC(CDiffViewer)

public:
    CDiffViewer();
    virtual ~CDiffViewer();

    // 打开窗口并立即对比 pkgA/pkgB 中 pathUtf8 指定条目的内容
    BOOL Create(const std::wstring& pkgA, const std::wstring& pkgB, const std::string& pathUtf8);

protected:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
    afx_msg void OnModeChanged();
    afx_msg void OnClose();
    afx_msg void OnScrollA();
    afx_msg void OnScrollB();
    virtual void PostNcDestroy();
    DECLARE_MESSAGE_MAP()

private:
    // 行级对齐计划：aIdx/bIdx 为该行在两侧行数组中的下标（-1 表示该侧无对应行）
    struct DiffRow {
        int aIdx = -1;
        int bIdx = -1;
        COLORREF bgA = kDiffBgSame;
        COLORREF bgB = kDiffBgSame;
    };

    void Layout();
    void BuildDiff();
    // 返回 false 表示行数超出 LCS 动态规划容量，退化为公共前后缀对齐
    bool BuildRowPlanText(const std::vector<std::wstring>& la, const std::vector<std::wstring>& lb,
                          std::vector<DiffRow>& rows);
    void BuildTextDiff();
    void BuildHexDiff();
    void AppendRuns(CRichEditCtrl& re, const std::vector<std::pair<std::wstring, COLORREF>>& items);
    void SyncVertScroll(CRichEditCtrl& from, CRichEditCtrl& to);
    void SetStatus(const CString& text);

    CStatic m_lblPath;
    CStatic m_lblMode;
    CComboBox m_cmbMode;
    CButton m_btnClose;
    CRichEditCtrl m_txtA;
    CRichEditCtrl m_txtB;
    CStatic m_status;
    CFont m_fontMono;

    std::wstring m_pkgA;
    std::wstring m_pkgB;
    std::string m_pathUtf8;
    std::vector<uint8_t> m_plainA;  // 两侧明文（OnCreate 一次解码，切模式复用）
    std::vector<uint8_t> m_plainB;
    bool m_okA = false;
    bool m_okB = false;
    std::string m_errA;
    std::string m_errB;
    bool m_syncing = false;  // 滚动同步防重入
};
