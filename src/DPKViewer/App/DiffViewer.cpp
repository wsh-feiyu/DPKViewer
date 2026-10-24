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
const double kMaxLcsCells = 12000000.0;  // 行级 LCS 动态规划单元上限，超出退化为前后缀对齐

const COLORREF kBgDiff = RGB(255, 235, 156);  // 内容修改：黄
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
    ON_BN_CLICKED(IDC_DIFF_CLOSE, &CDiffViewer::OnClose)
    ON_EN_VSCROLL(IDC_DIFF_TEXT_A, &CDiffViewer::OnScrollA)
    ON_EN_VSCROLL(IDC_DIFF_TEXT_B, &CDiffViewer::OnScrollB)
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

    m_lblPath.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_PATHELIPSIS, CRect(0, 0, 10, 10),
                     this, IDC_DIFF_PATH);
    m_lblMode.Create(L"模式:", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, CRect(0, 0, 10, 10), this, (UINT)-1);
    m_cmbMode.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL, CRect(0, 0, 10, 10), this,
                     IDC_DIFF_MODE);
    m_cmbMode.AddString(L"文本对比");
    m_cmbMode.AddString(L"十六进制对比");
    m_btnClose.Create(L"关闭(&X)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this, IDC_DIFF_CLOSE);

    const DWORD paneStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY |
                            ES_AUTOVSCROLL | ES_AUTOHSCROLL;
    m_txtA.Create(paneStyle, CRect(0, 0, 10, 10), this, IDC_DIFF_TEXT_A);
    m_txtB.Create(paneStyle, CRect(0, 0, 10, 10), this, IDC_DIFF_TEXT_B);
    m_txtA.LimitText(0x4000000);
    m_txtB.LimitText(0x4000000);

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
        m_lblPath.GetSafeHwnd(), m_lblMode.GetSafeHwnd(), m_cmbMode.GetSafeHwnd(), m_btnClose.GetSafeHwnd(),
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
    CenterWindow(GetParent());
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
    const int row2 = row1 + kRowH + kRowGap;

    m_lblPath.MoveWindow(kMargin, row1, right - kMargin, kRowH);

    const int lblModeW = 40;
    const int cmbW = 150;
    m_lblMode.MoveWindow(kMargin, row2, lblModeW, kRowH);
    m_cmbMode.MoveWindow(kMargin + lblModeW, row2, cmbW, 200);
    m_btnClose.MoveWindow(right - 74, row2, 74, kRowH);

    const int statusTop = rc.Height() - kStatusH;
    m_status.MoveWindow(kMargin, statusTop, right - kMargin, kStatusH);

    const int paneTop = row2 + kRowH + kRowGap;
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
void CDiffViewer::AppendRuns(CRichEditCtrl& re, const std::vector<std::pair<std::wstring, COLORREF>>& items) {
    re.SetRedraw(FALSE);
    size_t i = 0;
    while (i < items.size()) {
        size_t j = i;
        while (j < items.size() && items[j].second == items[i].second) ++j;
        std::wstring joined;
        for (size_t k = i; k < j; ++k) joined += items[k].first;
        CHARRANGE cr;
        cr.cpMin = -1;
        cr.cpMax = -1;
        re.SetSel(cr);
        CHARFORMAT2 cf;
        memset(&cf, 0, sizeof(cf));
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_BACKCOLOR;
        cf.crBackColor = items[i].second == kDiffBgSame ? ::GetSysColor(COLOR_WINDOW) : items[i].second;
        re.SetSelectionCharFormat(cf);
        re.ReplaceSel(CString(joined.c_str()));
        i = j;
    }
    re.SetRedraw(TRUE);
}

bool CDiffViewer::BuildRowPlanText(const std::vector<std::wstring>& la, const std::vector<std::wstring>& lb,
                                   std::vector<DiffRow>& rows) {
    const size_t n = la.size();
    const size_t m = lb.size();

    // 行文本 -> 整数 id，把字符串比较变成整数比较
    std::map<std::wstring, int> pool;
    auto intern = [&pool](const std::vector<std::wstring>& ls) {
        std::vector<int> ids;
        ids.reserve(ls.size());
        for (const std::wstring& s : ls) {
            auto it = pool.find(s);
            if (it == pool.end()) it = pool.emplace(s, (int)pool.size()).first;
            ids.push_back(it->second);
        }
        return ids;
    };
    const std::vector<int> va = intern(la);
    const std::vector<int> vb = intern(lb);

    if ((double)n * (double)m <= kMaxLcsCells) {
        // 经典 LCS 动态规划：dir 0=对角（行相等）1=上方 2=左方
        const size_t W = m + 1;
        std::vector<uint16_t> lcs((n + 1) * W, 0);
        std::vector<uint8_t> dir((n + 1) * W, 0);
        for (size_t i = 1; i <= n; ++i) {
            for (size_t j = 1; j <= m; ++j) {
                const size_t cur = i * W + j;
                if (va[i - 1] == vb[j - 1]) {
                    lcs[cur] = (uint16_t)(lcs[(i - 1) * W + j - 1] + 1);
                    dir[cur] = 0;
                } else if (lcs[(i - 1) * W + j] >= lcs[cur - 1]) {
                    lcs[cur] = lcs[(i - 1) * W + j];
                    dir[cur] = 1;
                } else {
                    lcs[cur] = lcs[cur - 1];
                    dir[cur] = 2;
                }
            }
        }
        std::vector<DiffRow> rev;
        size_t i = n, j = m;
        while (i > 0 || j > 0) {
            if (i > 0 && j > 0 && dir[i * W + j] == 0) {
                DiffRow r;
                r.aIdx = (int)(i - 1);
                r.bIdx = (int)(j - 1);
                rev.push_back(r);
                --i;
                --j;
            } else if (j > 0 && (i == 0 || dir[i * W + j] == 2)) {
                DiffRow r;
                r.bIdx = (int)(j - 1);
                r.bgB = kBgIns;
                rev.push_back(r);
                --j;
            } else {
                DiffRow r;
                r.aIdx = (int)(i - 1);
                r.bgA = kBgDel;
                rev.push_back(r);
                --i;
            }
        }
        std::reverse(rev.begin(), rev.end());
        rows.swap(rev);
        return true;
    }

    // 超出动态规划容量：对齐公共前缀/后缀，中段整体标记
    size_t p = 0;
    while (p < n && p < m && va[p] == vb[p]) ++p;
    size_t sa = n, sb = m;
    while (sa > p && sb > p && va[sa - 1] == vb[sb - 1]) {
        --sa;
        --sb;
    }
    for (size_t k = 0; k < p; ++k) {
        DiffRow r;
        r.aIdx = (int)k;
        r.bIdx = (int)k;
        rows.push_back(r);
    }
    for (size_t k = p; k < sa; ++k) {
        DiffRow r;
        r.aIdx = (int)k;
        r.bgA = kBgDel;
        r.bgB = kBgGap;
        rows.push_back(r);
    }
    for (size_t k = p; k < sb; ++k) {
        DiffRow r;
        r.bIdx = (int)k;
        r.bgB = kBgIns;
        r.bgA = kBgGap;
        rows.push_back(r);
    }
    const size_t tailPairs = std::min(n - sa, m - sb);
    for (size_t t = 0; t < tailPairs; ++t) {
        DiffRow r;
        r.aIdx = (int)(sa + t);
        r.bIdx = (int)(sb + t);
        rows.push_back(r);
    }
    return false;
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

    std::vector<DiffRow> rows;
    const bool precise = BuildRowPlanText(la, lb, rows);

    size_t same = 0, chg = 0, del = 0, ins = 0;
    for (const DiffRow& r : rows) {
        if (r.aIdx >= 0 && r.bIdx >= 0) {
            if (r.bgA == kDiffBgSame) {
                ++same;
            } else {
                ++chg;
            }
        } else if (r.aIdx >= 0) {
            ++del;
        } else {
            ++ins;
        }
    }

    for (const DiffRow& r : rows) {
        const bool hasA = r.aIdx >= 0;
        const bool hasB = r.bIdx >= 0;
        itemsA.emplace_back(hasA ? la[(size_t)r.aIdx] : std::wstring(L"·"), hasA ? r.bgA : kBgGap);
        itemsB.emplace_back(hasB ? lb[(size_t)r.bIdx] : std::wstring(L"·"), hasB ? r.bgB : kBgGap);
        itemsA.emplace_back(L"\r\n", kDiffBgSame);
        itemsB.emplace_back(L"\r\n", kDiffBgSame);
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

void CDiffViewer::OnScrollA() {
    SyncVertScroll(m_txtA, m_txtB);
}

void CDiffViewer::OnScrollB() {
    SyncVertScroll(m_txtB, m_txtA);
}

void CDiffViewer::SyncVertScroll(CRichEditCtrl& from, CRichEditCtrl& to) {
    if (m_syncing) return;
    m_syncing = true;
    const int pos = from.GetScrollPos(SB_VERT);
    to.SendMessage(WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, (WORD)pos), 0);
    m_syncing = false;
}
