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
    afx_msg void OnHideSameChanged();
    afx_msg void OnClose();
    virtual void PostNcDestroy();
    DECLARE_MESSAGE_MAP()

private:
    void Layout();
    void BuildDiff();
    void BuildTextDiff();
    void BuildHexDiff();
    void AppendRuns(CRichEditCtrl& re, const std::vector<std::pair<std::wstring, COLORREF>>& items);
    // 富文本滚动消息子类过程：任何一侧滚动（滚动条/滚轮/键盘）后立刻像素级同步另一侧
    static LRESULT CALLBACK PaneProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                                     DWORD_PTR refData);
    void SyncFrom(int paneId);
    void SetStatus(const CString& text);

    CStatic m_lblMode;
    CComboBox m_cmbMode;
    CButton m_chkHideSame;
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
