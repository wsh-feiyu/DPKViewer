// FileViewer.h - 单个条目内容查看窗口（文本 / 十六进制）
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <afxwin.h>
#include <afxcmn.h>

class CFileViewer : public CFrameWnd {
    DECLARE_DYNAMIC(CFileViewer)

public:
    CFileViewer();
    virtual ~CFileViewer();

    // plain 为已解码的条目明文，pathUtf8 用于标题
    BOOL Create(const std::vector<uint8_t>& plain, const std::string& pathUtf8);

protected:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
    afx_msg void OnModeChanged();
    afx_msg void OnClose();
    virtual void PostNcDestroy();
    DECLARE_MESSAGE_MAP()

private:
    void Layout();
    void BuildView();
    void SetStatus(const CString& text);

    CStatic m_lblMode;
    CComboBox m_cmbMode;
    CButton m_btnClose;
    CRichEditCtrl m_text;
    CStatic m_status;
    CFont m_fontMono;

    std::vector<uint8_t> m_plain;
    std::string m_pathUtf8;
    bool m_isText = false;
    bool m_garbled = false;  // 字节像文本但解码乱码，已自动改用十六进制
};
