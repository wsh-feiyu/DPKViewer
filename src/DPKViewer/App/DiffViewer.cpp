// DiffViewer.cpp - 条目内容对比查看窗口实现
// 文本内容：LCS 行级 diff，修改行黄 / A 独有红 / B 独有绿；
// 非文本内容：十六进制逐字节对齐，差异字节黄色高亮。
#include <afxwin.h>
#include <afxcmn.h>
#include <richedit.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <utility>

#include "DiffViewer.h"
#include "resource.h"

#include "Core/DpkReader.h"
#include "Core/StrUtil.h"
#include "Core/TextDiff.h"

namespace {

#ifndef SS_PATHELIPSIS
#define SS_PATHELIPSIS 0x00008000  // 部分精简头文件下未定义：路径省略显示
#endif

const int kMargin = 8;
const int kRowH = 24;
const int kRowGap = 6;
const int kStatusH = 22;

// 视图容量上限：防止超大条目卡死界面
const size_t kMaxTextLines = 10000;      // 文本模式每侧最多参与对比的行数
const size_t kMaxHexBytes = 128 * 1024;  // 十六进制模式每侧最多显示的字节数

const COLORREF kBgDiff = RGB(255, 235, 156);  // 内容修改的行：黄
const COLORREF kBgDeep = RGB(255, 168, 112);  // 行内差异片段：橙
const COLORREF kBgDel = RGB(255, 205, 205);   // A 独有（删除）：浅红
const COLORREF kBgIns = RGB(205, 245, 205);   // B 独有（新增）：浅绿
const COLORREF kBgNote = RGB(226, 239, 255);  // 说明行：浅蓝
const COLORREF kBgGap = RGB(242, 242, 242);   // 对侧留白：灰

CString W(const std::string& s) {
    return CString(dpk::Utf8ToWide(s).c_str());
}

// 从包里按路径读出条目明文（独立打开包，避免与对比窗口共享状态）
bool ReadPlainByPath(const std::wstring& pkg, const std::string& pathUtf8, std::vector<uint8_t>& plain,
                     std::string& err) {
    plain.clear();
    dpk::Reader reader;
    if (!reader.Open(pkg, err)) return false;
    if (!reader.LoadEntries(err, false)) return false;
    for (const dpk::Entry& e : reader.Entries()) {
        if (e.pathUtf8 != pathUtf8) continue;
        dpk::WhscInfo info;
        return reader.ReadEntry(e, plain, info, err);
    }
    err = "包内没有该条目";
    return false;
}

void SplitLines(const std::wstring& text, std::vector<std::wstring>& lines) {
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == L'\n') {
            size_t end = i;
            if (end > start && text[end - 1] == L'\r') --end;
            lines.emplace_back(text, start, end - start);
            start = i + 1;
        }
    }
}

}  // namespace

IMPLEMENT_DYNAMIC(CDiffViewer, CFrameWnd)

BEGIN_MESSAGE_MAP(CDiffViewer, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_WM_CLOSE()
    ON_CBN_SELCHANGE(IDC_DIFF_MODE, &CDiffViewer::OnModeChanged)
    ON_BN_CLICKED(IDC_DIFF_HIDESAME, &CDiffViewer::OnHideSameChanged)
    ON_BN_CLICKED(IDC_DIFF_CLOSE, &CDiffViewer::OnClose)
END_MESSAGE_MAP()

CDiffViewer::CDiffViewer() {}

CDiffViewer::~CDiffViewer() {}

BOOL CDiffViewer::Create(const std::wstring& pkgA, const std::wstring& pkgB, const std::string& pathUtf8) {
    m_pkgA = pkgA;
    m_pkgB = pkgB;
    m_pathUtf8 = pathUtf8;
    const CString title = L"内容对比 - " + W(pathUtf8);
    return CFrameWnd::Create(nullptr, title, WS_OVERLAPPEDWINDOW, CRect(0, 0, 1000, 640));
}

int CDiffViewer::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1) return -1;

    SetIcon(::LoadIcon(::GetModuleHandle(nullptr), MAKEINTRESOURCE(IDR_MAINFRAME)), TRUE);

    if (!AfxInitRichEdit2()) return -1;

    m_lblMode.Create(L"模式:", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, CRect(0, 0, 10, 10), this, (UINT)-1);
    m_cmbMode.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL, CRect(0, 0, 10, 10), this,
                     IDC_DIFF_MODE);
    m_cmbMode.AddString(L"文本对比");
    m_cmbMode.AddString(L"十六进制对比");
    m_chkHideSame.Create(L"隐藏相同行(&H)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, CRect(0, 0, 10, 10), this,
                         IDC_DIFF_HIDESAME);
    m_btnClose.Create(L"关闭(&X)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this, IDC_DIFF_CLOSE);

    const DWORD paneStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY |
                            ES_AUTOVSCROLL | ES_AUTOHSCROLL;
    m_txtA.Create(paneStyle, CRect(0, 0, 10, 10), this, IDC_DIFF_TEXT_A);
    m_txtB.Create(paneStyle, CRect(0, 0, 10, 10), this, IDC_DIFF_TEXT_B);
    m_txtA.LimitText(0x4000000);
    m_txtB.LimitText(0x4000000);
    // 子类化两个富文本：拦截滚动消息做像素级双侧同步（EN_VSCROLL 对滚轮不可靠）
    ::SetWindowSubclass(m_txtA.GetSafeHwnd(), PaneProc, 1, (DWORD_PTR)this);
    ::SetWindowSubclass(m_txtB.GetSafeHwnd(), PaneProc, 2, (DWORD_PTR)this);

    m_status.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_SUNKEN, CRect(0, 0, 10, 10), this,
                    IDC_DIFF_STATUS);

    // 等宽字体：Consolas 9pt
    {
        const int dpiY = ::GetDeviceCaps(GetDC()->GetSafeHdc(), LOGPIXELSY);
        const int pixelH = -MulDiv(9, dpiY, 72);
        m_fontMono.CreateFont(pixelH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    }
    HWND all[] = {
        m_lblMode.GetSafeHwnd(), m_cmbMode.GetSafeHwnd(), m_chkHideSame.GetSafeHwnd(), m_btnClose.GetSafeHwnd(),
        m_txtA.GetSafeHwnd(),    m_txtB.GetSafeHwnd(),    m_status.GetSafeHwnd(),
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
    m_txtA.SetDefaultCharFormat(cf);
    m_txtB.SetDefaultCharFormat(cf);

    Layout();

    // 解码两侧明文（一次解码，切换模式时复用）
    {
        CWaitCursor wait;
        m_okA = ReadPlainByPath(m_pkgA, m_pathUtf8, m_plainA, m_errA);
        m_okB = ReadPlainByPath(m_pkgB, m_pathUtf8, m_plainB, m_errB);
    }
    if (!m_okA || !m_okB) {
        CString s = L"读取";
        if (!m_okA) s += L" A";
        if (!m_okB) s += (!m_okA ? L" 和 B" : L" B");
        s += L" 失败";
        SetStatus(s);
    }

    // 自动选模式：两侧都像文本 -> 文本对比，否则十六进制
    auto looksText = [](const std::vector<uint8_t>& p, bool ok) {
        if (!ok) return false;
        if (p.empty()) return true;
        const size_t n = p.size() < 4096 ? p.size() : 4096;
        return dpk::LooksLikeText(&p[0], n);
    };
    m_cmbMode.SetCurSel(looksText(m_plainA, m_okA) && looksText(m_plainB, m_okB) ? 0 : 1);

    BuildDiff();
    CenterWindow(GetDesktopWindow());
    return 0;
}

void CDiffViewer::OnGetMinMaxInfo(MINMAXINFO* lpMMI) {
    lpMMI->ptMinTrackSize.x = 720;
    lpMMI->ptMinTrackSize.y = 460;
    CFrameWnd::OnGetMinMaxInfo(lpMMI);
}

void CDiffViewer::Layout() {
    if (m_txtA.GetSafeHwnd() == nullptr) return;
    CRect rc;
    GetClientRect(&rc);
    const int right = rc.Width() - kMargin;

    const int row1 = kMargin;

    const int lblModeW = 40;
    const int cmbW = 150;
    m_lblMode.MoveWindow(kMargin, row1, lblModeW, kRowH);
    m_cmbMode.MoveWindow(kMargin + lblModeW, row1, cmbW, 200);
    m_chkHideSame.MoveWindow(kMargin + lblModeW + cmbW + kRowGap, row1, 116, kRowH);
    m_btnClose.MoveWindow(right - 74, row1, 74, kRowH);

    const int statusTop = rc.Height() - kStatusH;
    m_status.MoveWindow(kMargin, statusTop, right - kMargin, kStatusH);

    const int paneTop = row1 + kRowH + kRowGap;
    int paneH = statusTop - paneTop - kRowGap;
    if (paneH < 80) paneH = 80;
    const int gap = 6;
    const int paneW = (right - kMargin - gap) / 2;
    m_txtA.MoveWindow(kMargin, paneTop, paneW, paneH);
    m_txtB.MoveWindow(kMargin + paneW + gap, paneTop, right - (kMargin + paneW + gap), paneH);
}

void CDiffViewer::OnSize(UINT nType, int cx, int cy) {
    CFrameWnd::OnSize(nType, cx, cy);
    if (nType != SIZE_MINIMIZED) Layout();
}

void CDiffViewer::SetStatus(const CString& text) {
    if (m_status.GetSafeHwnd() != nullptr) m_status.SetWindowText(text);
}

void CDiffViewer::BuildDiff() {
    if (m_txtA.GetSafeHwnd() == nullptr) return;
    m_txtA.SetWindowText(L"");
    m_txtB.SetWindowText(L"");
    if (m_cmbMode.GetCurSel() == 1) {
        BuildHexDiff();
    } else {
        BuildTextDiff();
    }
    m_txtA.Invalidate();
    m_txtB.Invalidate();
}

// 追加带背景色的文本段；相邻同色段合并为一次 ReplaceSel，避免逐行调用卡顿
// 追加带背景色的文本段——一次性生成 RTF 流输入。
// 之前用逐段 SetSelectionCharFormat+ReplaceSel，段数多时富文本反复重排版（平方级），
// 大文本（50KB+）要卡几秒；RTF 单遍载入基本瞬间完成。
void CDiffViewer::AppendRuns(CRichEditCtrl& re, const std::vector<std::pair<std::wstring, COLORREF>>& items) {
    static const struct {
        COLORREF c;
        int hl;
    } kMap[] = {
        {kBgDiff, 1}, {kBgDel, 2}, {kBgIns, 3}, {kBgNote, 4}, {kBgGap, 5}, {kBgDeep, 6},
    };
    auto hlOf = [](COLORREF c) -> int {
        for (const auto& m : kMap) {
            if (m.c == c) return m.hl;
        }
        return 0;  // 默认背景
    };
    // RTF 转义：ASCII 直出，控制字转义，非 ASCII 用 \uN?（16 位有符号）
    auto escape = [](const std::wstring& s, std::string& out) {
        char buf[24];
        for (const wchar_t c : s) {
            if (c == L'\\' || c == L'{' || c == L'}') {
                out += '\\';
                out += (char)c;
            } else if (c == L'\r') {
                continue;
            } else if (c == L'\n') {
                out += "\\par ";
            } else if (c < 128) {
                out += (char)c;
            } else {
                sprintf_s(buf, "\\u%d?", (short)c);
                out += buf;
            }
        }
    };

    std::string rtf =
        "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fmodern Consolas;}}\\fs18"
        "{\\colortbl;\\red255\\green235\\blue156;\\red255\\green205\\blue205;\\red205\\green245\\blue205;"
        "\\red226\\green239\\blue255;\\red242\\green242\\blue242;\\red255\\green168\\blue112;}";
    char hl[24];
    size_t i = 0;
    while (i < items.size()) {
        size_t j = i;
        while (j < items.size() && items[j].second == items[i].second) ++j;
        const int h = hlOf(items[i].second);
        if (h != 0) {
            sprintf_s(hl, "\\highlight%d ", h);
            rtf += hl;
        }
        for (size_t k = i; k < j; ++k) escape(items[k].first, rtf);
        if (h != 0) rtf += "\\highlight0 ";
        i = j;
    }
    rtf += "}";

    struct StreamCtx {
        const char* p;
        size_t left;
    } ctx = {rtf.data(), rtf.size()};
    auto cb = [](DWORD_PTR cookie, LPBYTE buf, LONG count, LONG* done) -> DWORD CALLBACK {
        StreamCtx* c = reinterpret_cast<StreamCtx*>(cookie);
        const LONG n = c->left < (size_t)count ? (LONG)c->left : count;
        if (n > 0) memcpy(buf, c->p, (size_t)n);
        c->p += n;
        c->left -= (size_t)n;
        *done = n;
        return 0;
    };
    EDITSTREAM es;
    es.dwCookie = (DWORD_PTR)&ctx;
    es.dwError = 0;
    es.pfnCallback = cb;
    re.SetRedraw(FALSE);
    re.StreamIn(SF_RTF, es);
    re.SetSel(0, 0);  // 回到顶部
    re.SetRedraw(TRUE);
}

void CDiffViewer::BuildTextDiff() {
    // 整段解码为宽字符文本（UTF-8 严格优先，失败回退系统 ANSI 代码页）
    auto toText = [](const std::vector<uint8_t>& p) {
        return p.empty() ? std::wstring() : dpk::NameBytesToWide(std::string((const char*)&p[0], p.size()));
    };
    std::vector<std::wstring> la, lb;
    if (m_okA) SplitLines(toText(m_plainA), la);
    if (m_okB) SplitLines(toText(m_plainB), lb);

    std::vector<std::pair<std::wstring, COLORREF>> itemsA, itemsB;
    itemsA.emplace_back(L"── A 侧 ──\r\n", kBgNote);
    itemsB.emplace_back(L"── B 侧 ──\r\n", kBgNote);
    if (!m_okA) itemsA.emplace_back(W("读取失败: " + m_errA) + L"\r\n", kBgNote);
    if (!m_okB) itemsB.emplace_back(W("读取失败: " + m_errB) + L"\r\n", kBgNote);

    bool capped = false;
    if (la.size() > kMaxTextLines) {
        la.resize(kMaxTextLines);
        capped = true;
    }
    if (lb.size() > kMaxTextLines) {
        lb.resize(kMaxTextLines);
        capped = true;
    }

    // 行级对齐：精确 LCS + 相似行二次配对（Core/TextDiff）
    std::vector<dpk::TextDiffRow> rows;
    const bool precise = dpk::TextDiffRows(la, lb, rows);

    size_t same = 0, chg = 0, del = 0, ins = 0;
    for (const dpk::TextDiffRow& r : rows) {
        switch (r.kind) {
            case 0: ++same; break;
            case 1: ++chg; break;
            case 2: ++del; break;
            default: ++ins; break;
        }
    }

    // 行号前缀（等宽字体下右对齐 5 位）与留白
    auto lineNo = [](int n) {
        wchar_t buf[16];
        swprintf_s(buf, 16, L"%5d ", n);
        return std::wstring(buf);
    };
    auto lineGap = []() { return std::wstring(L"      "); };

    const bool hideSame = m_chkHideSame.GetSafeHwnd() != nullptr && m_chkHideSame.GetCheck() == BST_CHECKED;

    // 发射一行（带行号；部分修改行内部用橙色标出差异片段）
    auto emitRow = [&](const dpk::TextDiffRow& r) {
        const bool hasA = r.aIdx >= 0;
        const bool hasB = r.bIdx >= 0;
        const std::wstring nA = hasA ? lineNo(r.aIdx + 1) : lineGap();
        const std::wstring nB = hasB ? lineNo(r.bIdx + 1) : lineGap();
        if (r.kind == 1 && hasA && hasB) {
            const std::wstring& sa = la[(size_t)r.aIdx];
            const std::wstring& sb = lb[(size_t)r.bIdx];
            const size_t p = dpk::TextCommonPrefix(sa, sb);
            const size_t s = dpk::TextCommonSuffix(sa, sb, std::min(sa.size(), sb.size()) - p);
            itemsA.emplace_back(nA + sa.substr(0, p), kBgDiff);
            if (sa.size() > p + s) itemsA.emplace_back(sa.substr(p, sa.size() - p - s), kBgDeep);
            itemsA.emplace_back(sa.substr(sa.size() - s), kBgDiff);
            itemsA.emplace_back(L"\r\n", kDiffBgSame);
            itemsB.emplace_back(nB + sb.substr(0, p), kBgDiff);
            if (sb.size() > p + s) itemsB.emplace_back(sb.substr(p, sb.size() - p - s), kBgDeep);
            itemsB.emplace_back(sb.substr(sb.size() - s), kBgDiff);
            itemsB.emplace_back(L"\r\n", kDiffBgSame);
        } else {
            COLORREF bgA = kDiffBgSame;
            COLORREF bgB = kDiffBgSame;
            if (r.kind == 2) {
                bgA = kBgDel;
                bgB = kBgGap;
            } else if (r.kind == 3) {
                bgB = kBgIns;
                bgA = kBgGap;
            }
            itemsA.emplace_back(nA + (hasA ? la[(size_t)r.aIdx] : std::wstring(L"·")), bgA);
            itemsB.emplace_back(nB + (hasB ? lb[(size_t)r.bIdx] : std::wstring(L"·")), bgB);
            itemsA.emplace_back(L"\r\n", kDiffBgSame);
            itemsB.emplace_back(L"\r\n", kDiffBgSame);
        }
    };

    // 连续相同行（>=3 行）在勾选「隐藏相同行」时折叠为一行占位提示
    size_t ri = 0;
    while (ri < rows.size()) {
        if (rows[ri].kind == 0 && hideSame) {
            size_t rj = ri;
            while (rj < rows.size() && rows[rj].kind == 0) ++rj;
            if (rj - ri >= 3) {
                wchar_t buf[128];
                swprintf_s(buf, 128, L"----- 行 A%d-%d / B%d-%d 相同（共 %zu 行，已折叠） -----",
                           rows[ri].aIdx + 1, rows[rj - 1].aIdx + 1, rows[ri].bIdx + 1, rows[rj - 1].bIdx + 1,
                           rj - ri);
                const std::wstring ph(buf);
                itemsA.emplace_back(ph, kBgNote);
                itemsA.emplace_back(L"\r\n", kDiffBgSame);
                itemsB.emplace_back(ph, kBgNote);
                itemsB.emplace_back(L"\r\n", kDiffBgSame);
                ri = rj;
                continue;
            }
        }
        emitRow(rows[ri]);
        ++ri;
    }
    if (capped) {
        CString note;
        note.Format(L"（行数超过 %u，仅对比前 %u 行）\r\n", (unsigned)kMaxTextLines, (unsigned)kMaxTextLines);
        itemsA.emplace_back(std::wstring(note), kBgNote);
        itemsB.emplace_back(std::wstring(note), kBgNote);
    } else if (!precise) {
        itemsA.emplace_back(L"（行数过多，快速模式：仅对齐公共前后缀，中段整体标记）\r\n", kBgNote);
        itemsB.emplace_back(L"（行数过多，快速模式：仅对齐公共前后缀，中段整体标记）\r\n", kBgNote);
    }

    AppendRuns(m_txtA, itemsA);
    AppendRuns(m_txtB, itemsB);

    CString stats;
    stats.Format(L"文本对比: 相同 %u 行，修改 %u 行，A 独有 %u 行，B 独有 %u 行%s", (unsigned)same, (unsigned)chg,
                 (unsigned)del, (unsigned)ins, capped ? L"（已截断）" : L"");
    SetStatus(stats);
}

void CDiffViewer::BuildHexDiff() {
    std::vector<std::pair<std::wstring, COLORREF>> itemsA, itemsB;
    itemsA.emplace_back(L"── A 侧（十六进制） ──\r\n", kBgNote);
    itemsB.emplace_back(L"── B 侧（十六进制） ──\r\n", kBgNote);
    if (!m_okA) itemsA.emplace_back(W("读取失败: " + m_errA) + L"\r\n", kBgNote);
    if (!m_okB) itemsB.emplace_back(W("读取失败: " + m_errB) + L"\r\n", kBgNote);

    const size_t lenA = m_plainA.size();
    const size_t lenB = m_plainB.size();
    const size_t viewLen = std::min(std::max(lenA, lenB), kMaxHexBytes);

    for (size_t off = 0; off < viewLen; off += 16) {
        CString offs;
        offs.Format(L"%08X", (unsigned)off);
        itemsA.emplace_back(std::wstring(offs) + L"  ", kDiffBgSame);
        itemsB.emplace_back(std::wstring(offs) + L"  ", kDiffBgSame);
        for (int side = 0; side < 2; ++side) {
            const std::vector<uint8_t>& own = side == 0 ? m_plainA : m_plainB;
            const std::vector<uint8_t>& other = side == 0 ? m_plainB : m_plainA;
            auto& items = side == 0 ? itemsA : itemsB;
            for (int k = 0; k < 16; ++k) {
                const size_t idx = off + (size_t)k;
                const bool hasOwn = idx < own.size();
                const bool hasOther = idx < other.size();
                const bool diff = !hasOwn || !hasOther || own[idx] != other[idx];
                wchar_t buf[4] = L"  ";
                if (hasOwn) swprintf(buf, 4, L"%02X", own[idx]);
                items.emplace_back(std::wstring(buf) + L" ", diff ? kBgDiff : kDiffBgSame);
            }
            items.emplace_back(L" ", kDiffBgSame);
            for (int k = 0; k < 16; ++k) {
                const size_t idx = off + (size_t)k;
                const bool hasOwn = idx < own.size();
                const bool hasOther = idx < other.size();
                const bool diff = !hasOwn || !hasOther || own[idx] != other[idx];
                wchar_t ch = L'?';
                if (hasOwn) {
                    const uint8_t b = own[idx];
                    ch = (b >= 0x20 && b < 0x7F) ? (wchar_t)b : L'.';
                }
                items.emplace_back(std::wstring(1, ch), diff ? kBgDiff : kDiffBgSame);
            }
            items.emplace_back(L"\r\n", kDiffBgSame);
        }
    }
    if (std::max(lenA, lenB) > kMaxHexBytes) {
        CString note;
        note.Format(L"（内容超过 %u KB，仅显示前 %u KB）\r\n", (unsigned)(kMaxHexBytes / 1024),
                    (unsigned)(kMaxHexBytes / 1024));
        itemsA.emplace_back(std::wstring(note), kBgNote);
        itemsB.emplace_back(std::wstring(note), kBgNote);
    }

    AppendRuns(m_txtA, itemsA);
    AppendRuns(m_txtB, itemsB);

    size_t diffBytes = lenA > lenB ? lenA - lenB : lenB - lenA;
    if (m_okA && m_okB) {
        const size_t minLen = std::min(lenA, lenB);
        for (size_t i = 0; i < minLen; ++i) {
            if (m_plainA[i] != m_plainB[i]) ++diffBytes;
        }
    }
    CString stats;
    stats.Format(L"十六进制对比: A %s，B %s，差异 %u 字节%s", (LPCTSTR)W(dpk::FormatBytes(lenA)),
                 (LPCTSTR)W(dpk::FormatBytes(lenB)), (unsigned)diffBytes, (lenA != lenB) ? L"（长度不同）" : L"");
    SetStatus(stats);
}

void CDiffViewer::OnModeChanged() {
    BuildDiff();
}

void CDiffViewer::OnClose() {
    DestroyWindow();
}

void CDiffViewer::PostNcDestroy() {
    delete this;
}

void CDiffViewer::OnHideSameChanged() {
    if (m_txtA.GetSafeHwnd() == nullptr) return;
    BuildDiff();
}

// 子类过程：消息走完默认处理后，若是滚动类消息则同步另一侧。
// 我们自己发出的 EM_SETSCROLLPOS 会被 m_syncing 拦截，不会往复读位。
LRESULT CALLBACK CDiffViewer::PaneProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                                       DWORD_PTR refData) {
    const LRESULT result = ::DefSubclassProc(hWnd, msg, wParam, lParam);
#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif
    if (msg == WM_VSCROLL || msg == WM_HSCROLL || msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL ||
        msg == WM_KEYDOWN) {
        CDiffViewer* self = reinterpret_cast<CDiffViewer*>(refData);
        if (self != nullptr && !self->m_syncing) self->SyncFrom((int)id);
    }
    return result;
}

void CDiffViewer::SyncFrom(int paneId) {
    CRichEditCtrl& from = paneId == 1 ? m_txtA : m_txtB;
    CRichEditCtrl& to = paneId == 1 ? m_txtB : m_txtA;
    POINT pf = {0, 0}, pt = {0, 0};
    from.SendMessage(EM_GETSCROLLPOS, 0, (LPARAM)&pf);
    to.SendMessage(EM_GETSCROLLPOS, 0, (LPARAM)&pt);
    if (pt.y == pf.y && pt.x == pf.x) return;  // 已同步（也防止往复触发）
    m_syncing = true;
    pt.x = pf.x;  // 水平/垂直都同步
    pt.y = pf.y;
    to.SendMessage(EM_SETSCROLLPOS, 0, (LPARAM)&pt);
    m_syncing = false;
}
