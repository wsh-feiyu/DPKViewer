// FileViewer.cpp - 单个条目内容查看窗口实现
// 文本内容直接显示（UTF-8 优先，回退 ANSI）；非文本按十六进制转储显示。
#include <afxwin.h>
#include <afxcmn.h>

#include "FileViewer.h"
#include "resource.h"

#include "Core/StrUtil.h"

namespace {

const int kMargin = 8;
const int kRowH = 24;
const int kRowGap = 6;
const int kStatusH = 22;

// 视图容量上限：防止超大条目卡死界面
const size_t kMaxTextBytes = 1 * 1024 * 1024;   // 文本模式最多显示的字节数
const size_t kMaxHexBytes = 512 * 1024;         // 十六进制模式最多显示的字节数

// 乱码检测：解码后替换字符（U+FFFD）与控制字符占比超过 1% 视为不可信文本
// 覆盖两种情况：UTF-8 严格解码失败回退 ANSI 后仍是乱码；二进制被当作文本解码
bool TextDecodedSane(const std::wstring& text) {
    const size_t n = text.size() < 32768 ? text.size() : 32768;
    if (n == 0) return true;
    size_t bad = 0;
    for (size_t i = 0; i < n; ++i) {
        const wchar_t c = text[i];
        if (c == 0xFFFD) {
            ++bad;
        } else if (c < 0x20 && c != L'\r' && c != L'\n' && c != L'\t') {
            ++bad;
        }
    }
    return bad * 100 <= n;
}

}  // namespace

IMPLEMENT_DYNAMIC(CFileViewer, CFrameWnd)

BEGIN_MESSAGE_MAP(CFileViewer, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_WM_CLOSE()
    ON_CBN_SELCHANGE(IDC_VIEW_MODE, &CFileViewer::OnModeChanged)
    ON_BN_CLICKED(IDC_VIEW_CLOSE, &CFileViewer::OnClose)
END_MESSAGE_MAP()

CFileViewer::CFileViewer() {}

CFileViewer::~CFileViewer() {}

BOOL CFileViewer::Create(const std::vector<uint8_t>& plain, const std::string& pathUtf8) {
    m_plain = plain;
    m_pathUtf8 = pathUtf8;
    const CString title = L"查看 - " + CString(dpk::Utf8ToWide(pathUtf8).c_str());
    return CFrameWnd::Create(nullptr, title, WS_OVERLAPPEDWINDOW, CRect(0, 0, 860, 600));
}

int CFileViewer::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1) return -1;

    SetIcon(::LoadIcon(::GetModuleHandle(nullptr), MAKEINTRESOURCE(IDR_MAINFRAME)), TRUE);

    if (!AfxInitRichEdit2()) return -1;

    m_lblMode.Create(L"模式:", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, CRect(0, 0, 10, 10), this, (UINT)-1);
    m_cmbMode.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL, CRect(0, 0, 10, 10), this,
                     IDC_VIEW_MODE);
    m_cmbMode.AddString(L"文本");
    m_cmbMode.AddString(L"十六进制");
    m_btnClose.Create(L"关闭(&X)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this, IDC_VIEW_CLOSE);

    m_text.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY |
                      ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                  CRect(0, 0, 10, 10), this, IDC_VIEW_TEXT);
    m_text.LimitText(0x4000000);

    m_status.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_SUNKEN, CRect(0, 0, 10, 10), this,
                    IDC_VIEW_STATUS);

    // 等宽字体：Consolas 9pt
    {
        const int dpiY = ::GetDeviceCaps(GetDC()->GetSafeHdc(), LOGPIXELSY);
        const int pixelH = -MulDiv(9, dpiY, 72);
        m_fontMono.CreateFont(pixelH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    }
    HWND all[] = {
        m_lblMode.GetSafeHwnd(), m_cmbMode.GetSafeHwnd(), m_btnClose.GetSafeHwnd(), m_text.GetSafeHwnd(),
        m_status.GetSafeHwnd(),
    };
    for (HWND h : all) {
        if (h) ::SendMessage(h, WM_SETFONT, (WPARAM)m_fontMono.GetSafeHandle(), TRUE);
    }

    CHARFORMAT2 cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR;
    cf.yHeight = 180;  // 9pt（twips）
    cf.crTextColor = ::GetSysColor(COLOR_WINDOWTEXT);
    cf.bCharSet = DEFAULT_CHARSET;
    cf.bPitchAndFamily = FIXED_PITCH | FF_MODERN;
    wcscpy_s(cf.szFaceName, L"Consolas");
    m_text.SetDefaultCharFormat(cf);

    // 自动选模式：开头像文本且解码后无乱码 -> 文本，否则十六进制
    const size_t n = m_plain.size() < 4096 ? m_plain.size() : 4096;
    const bool likeText = m_plain.empty() || (n > 0 && dpk::LooksLikeText(&m_plain[0], n));
    bool sane = true;
    if (likeText && !m_plain.empty()) {
        const size_t sampleLen = m_plain.size() < 32768 ? m_plain.size() : 32768;
        sane = TextDecodedSane(dpk::NameBytesToWide(std::string((const char*)&m_plain[0], sampleLen)));
    }
    m_isText = likeText && sane;
    m_garbled = likeText && !sane;
    m_cmbMode.SetCurSel(m_isText ? 0 : 1);

    Layout();
    BuildView();
    CenterWindow(GetDesktopWindow());
    return 0;
}

void CFileViewer::OnGetMinMaxInfo(MINMAXINFO* lpMMI) {
    lpMMI->ptMinTrackSize.x = 560;
    lpMMI->ptMinTrackSize.y = 380;
    CFrameWnd::OnGetMinMaxInfo(lpMMI);
}

void CFileViewer::Layout() {
    if (m_text.GetSafeHwnd() == nullptr) return;
    CRect rc;
    GetClientRect(&rc);
    const int right = rc.Width() - kMargin;

    const int row1 = kMargin;
    const int lblW = 40;
    m_lblMode.MoveWindow(kMargin, row1, lblW, kRowH);
    m_cmbMode.MoveWindow(kMargin + lblW, row1, 130, 200);
    m_btnClose.MoveWindow(right - 74, row1, 74, kRowH);

    const int statusTop = rc.Height() - kStatusH;
    m_status.MoveWindow(kMargin, statusTop, right - kMargin, kStatusH);

    const int top = row1 + kRowH + kRowGap;
    int h = statusTop - top - kRowGap;
    if (h < 80) h = 80;
    m_text.MoveWindow(kMargin, top, right - kMargin, h);
}

void CFileViewer::OnSize(UINT nType, int cx, int cy) {
    CFrameWnd::OnSize(nType, cx, cy);
    if (nType != SIZE_MINIMIZED) Layout();
}

void CFileViewer::SetStatus(const CString& text) {
    if (m_status.GetSafeHwnd() != nullptr) m_status.SetWindowText(text);
}

void CFileViewer::BuildView() {
    if (m_text.GetSafeHwnd() == nullptr) return;
    if (m_cmbMode.GetCurSel() == 0) {
        if (m_plain.empty()) {
            m_text.SetWindowText(L"（空文件）");
            SetStatus(L"文本模式 | 0 B | 完整显示 | 编码未知");
            return;
        }
        // 文本模式：整段按 UTF-8 优先解码（失败回退 ANSI），超限截断
        std::string raw((const char*)&m_plain[0], m_plain.size());
        const size_t encSample = raw.size() < 32768 ? raw.size() : 32768;
        const int enc = dpk::DetectTextEncoding(std::string(raw, 0, encSample));
        const wchar_t* encName = enc == 3   ? L"UTF-8 (BOM)"
                                 : enc == 1 ? L"UTF-8"
                                 : enc == 2 ? L"GBK (ANSI)"
                                            : L"未知";
        const bool capped = raw.size() > kMaxTextBytes;
        if (capped) raw.resize(kMaxTextBytes);
        // UTF-8 BOM 不参加显示
        if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB &&
            (unsigned char)raw[2] == 0xBF) {
            raw.erase(0, 3);
        }
        std::wstring text = dpk::NameBytesToWide(raw);
        if (capped) text += L"\r\n…（内容超过 1 MB，仅显示前 1 MB，完整内容请导出后查看）";
        m_text.SetWindowText(text.empty() ? CString(L"（空文件）") : CString(text.c_str()));
        CString st;
        st.Format(L"文本模式 | %s | %s | 编码 %s", (LPCTSTR)CString(dpk::FormatBytes(m_plain.size()).c_str()),
                  capped ? L"已截断" : L"完整显示", encName);
        SetStatus(st);
    } else {
        // 十六进制模式：偏移 + 16 字节 + ASCII，超限截断
        const std::string dump =
            dpk::HexDump(m_plain.empty() ? nullptr : &m_plain[0], m_plain.size(), kMaxHexBytes);
        m_text.SetWindowText(dump.empty() ? CString(L"（空文件）") : CString(dump.c_str()));
        CString st;
        st.Format(L"十六进制模式 | %s | %s%s", (LPCTSTR)CString(dpk::FormatBytes(m_plain.size()).c_str()),
                  m_plain.size() > kMaxHexBytes ? L"已截断" : L"完整显示",
                  m_garbled ? L" | 检测到文本解码乱码，已自动使用十六进制" : L"");
        SetStatus(st);
    }
}

void CFileViewer::OnModeChanged() {
    BuildView();
}

void CFileViewer::OnClose() {
    DestroyWindow();
}

void CFileViewer::PostNcDestroy() {
    delete this;
}
