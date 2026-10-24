// CompareWindow.cpp - DPK 包对比窗口实现
#include <afxwin.h>
#include <afxcmn.h>
#include <afxdlgs.h>

#include "CompareWindow.h"
#include "AppUtil.h"
#include "DiffViewer.h"
#include "resource.h"

#include "Core/DpkCompare.h"
#include "Core/FileIO.h"
#include "Core/StrUtil.h"
#include "Core/Whsc.h"

namespace {

const int kMargin = 8;
const int kRowH = 26;
const int kRowGap = 6;
const int kDetailH = 120;
const int kPkgInfoH = 34;
const int kProgressH = 16;
const int kStatusH = 22;

CString W(const std::string& s) {
    return CString(dpk::Utf8ToWide(s).c_str());
}

CString W(const std::wstring& s) {
    return CString(s.c_str());
}

CString BytesText(uint64_t n) {
    return W(dpk::FormatBytes(n));
}

CString FlagsText(const dpk::WhscInfo& info) {
    CString s;
    if (info.compressed) s += L"压缩";
    if (info.encrypted) {
        if (!s.IsEmpty()) s += L"+";
        s += info.contentPassword ? L"加密(内容口令)" : L"加密(包口令)";
    }
    if (s.IsEmpty()) s = L"原样";
    return s;
}

CString Md5HeadText(const uint8_t md5[16]) {
    CString s;
    for (int i = 0; i < 4; ++i) s.AppendFormat(L"%02x", md5[i]);
    s += L"…";
    return s;
}

CString StatusText(dpk::CompareStatus st) {
    switch (st) {
        case dpk::CompareStatus::Same: return L"相同";
        case dpk::CompareStatus::Different: return L"不同";
        case dpk::CompareStatus::OnlyA: return L"仅 A";
        case dpk::CompareStatus::OnlyB: return L"仅 B";
        case dpk::CompareStatus::Error: return L"错误";
    }
    return L"?";
}

COLORREF StatusColor(dpk::CompareStatus st) {
    switch (st) {
        case dpk::CompareStatus::Same: return RGB(16, 124, 16);
        case dpk::CompareStatus::Different: return RGB(196, 43, 28);
        case dpk::CompareStatus::OnlyA: return RGB(0, 103, 192);
        case dpk::CompareStatus::OnlyB: return RGB(135, 100, 184);
        case dpk::CompareStatus::Error: return RGB(164, 38, 44);
    }
    return ::GetSysColor(COLOR_WINDOWTEXT);
}

// 「明文 / 存储」一列的文字
CString SideSizeText(const dpk::CompareSideInfo& side) {
    if (!side.present) return L"—";
    if (side.headerOk || side.decoded) {
        return BytesText(side.header.plainSize) + L" / " + BytesText(side.header.storedSize);
    }
    return side.err.empty() ? L"存在" : L"读取失败";
}

struct BusyGuard {
    explicit BusyGuard(bool& flag) : flag_(flag) { flag_ = true; }
    ~BusyGuard() { flag_ = false; }
    bool& flag_;
};

}  // namespace

IMPLEMENT_DYNAMIC(CCompareWindow, CFrameWnd)

BEGIN_MESSAGE_MAP(CCompareWindow, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_WM_CLOSE()
    ON_BN_CLICKED(IDC_CMP_BTN_A, &CCompareWindow::OnBrowseA)
    ON_BN_CLICKED(IDC_CMP_BTN_B, &CCompareWindow::OnBrowseB)
    ON_BN_CLICKED(IDC_CMP_RUN, &CCompareWindow::OnRun)
    ON_BN_CLICKED(IDC_CMP_EXPORT, &CCompareWindow::OnExport)
    ON_BN_CLICKED(IDC_CMP_CLOSE, &CCompareWindow::OnClose)
    ON_CBN_SELCHANGE(IDC_CMP_FILTER, &CCompareWindow::OnFilterChanged)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_CMP_LIST, &CCompareWindow::OnEntryChanged)
    ON_NOTIFY(NM_DBLCLK, IDC_CMP_LIST, &CCompareWindow::OnEntryDblClk)
    ON_NOTIFY(LVN_GETDISPINFO, IDC_CMP_LIST, &CCompareWindow::OnGetdispinfo)
    ON_NOTIFY(NM_CUSTOMDRAW, IDC_CMP_LIST, &CCompareWindow::OnCustomDraw)
END_MESSAGE_MAP()

CCompareWindow::CCompareWindow() {}

CCompareWindow::~CCompareWindow() {}

BOOL CCompareWindow::Create(CWnd* pParent) {
    return CFrameWnd::Create(nullptr, L"DPK 包对比", WS_OVERLAPPEDWINDOW, CRect(0, 0, 940, 680), pParent);
}

int CCompareWindow::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1) return -1;

    SetIcon(::LoadIcon(::GetModuleHandle(nullptr), MAKEINTRESOURCE(IDR_MAINFRAME)), TRUE);

    m_lblA.Create(L"包 A(&A):", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE, CRect(0, 0, 10, 10), this,
                  (UINT)-1);
    m_edtA.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, CRect(0, 0, 10, 10), this, IDC_CMP_EDIT_A);
    m_btnBrowseA.Create(L"浏览…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this, IDC_CMP_BTN_A);

    m_lblB.Create(L"包 B(&B):", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE, CRect(0, 0, 10, 10), this,
                  (UINT)-1);
    m_edtB.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, CRect(0, 0, 10, 10), this, IDC_CMP_EDIT_B);
    m_btnBrowseB.Create(L"浏览…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this, IDC_CMP_BTN_B);

    m_lblDepth.Create(L"对比深度(&D):", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE, CRect(0, 0, 10, 10), this,
                      (UINT)-1);
    m_cmbDepth.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL, CRect(0, 0, 10, 10), this,
                      IDC_CMP_DEPTH);
    m_cmbDepth.AddString(L"仅路径 —— 只比条目清单（最快）");
    m_cmbDepth.AddString(L"标准 —— 路径 + 流头 + 内嵌 MD5（推荐）");
    m_cmbDepth.AddString(L"完整 —— 逐条目解码比对（慢，可检出损坏）");
    m_cmbDepth.SetCurSel(1);

    // 大小差异上限：可输入任意 MB 数值；「不限制」/ 0 / 留空 表示不检查
    m_lblLimit.Create(L"大小差异上限(MB):", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE, CRect(0, 0, 10, 10),
                      this, (UINT)-1);
    m_cmbLimit.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWN | WS_VSCROLL, CRect(0, 0, 10, 10), this,
                      IDC_CMP_SIZELIMIT);
    m_cmbLimit.AddString(L"不限制");
    m_cmbLimit.AddString(L"50");
    m_cmbLimit.AddString(L"100");
    m_cmbLimit.AddString(L"200");
    m_cmbLimit.AddString(L"500");
    m_cmbLimit.AddString(L"1024");
    m_cmbLimit.SetCurSel(2);  // 默认 100 MB
    m_btnRun.Create(L"开始对比(&C)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this, IDC_CMP_RUN);
    m_btnExport.Create(L"导出报告(&S)…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this,
                       IDC_CMP_EXPORT);
    m_btnClose.Create(L"关闭(&X)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this, IDC_CMP_CLOSE);

    m_lblFilter.Create(L"筛选:", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE, CRect(0, 0, 10, 10), this,
                       (UINT)-1);
    m_cmbFilter.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL, CRect(0, 0, 10, 10), this,
                       IDC_CMP_FILTER);
    m_cmbFilter.AddString(L"全部条目");
    m_cmbFilter.AddString(L"仅差异（不同/仅A/仅B/错误）");
    m_cmbFilter.AddString(L"仅内容不同");
    m_cmbFilter.AddString(L"仅 A 独有");
    m_cmbFilter.AddString(L"仅 B 独有");
    m_cmbFilter.AddString(L"仅相同");
    m_cmbFilter.SetCurSel(0);
    m_lblStats.Create(L"未对比", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, CRect(0, 0, 10, 10), this,
                      IDC_CMP_STATS);

    m_list.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS |
                      LVS_NOSORTHEADER | LVS_OWNERDATA,
                  CRect(0, 0, 10, 10), this, IDC_CMP_LIST);
    m_list.SetExtendedStyle(m_list.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    m_list.InsertColumn(0, L"状态", LVCFMT_LEFT, 56);
    m_list.InsertColumn(1, L"条目路径", LVCFMT_LEFT, 360);
    m_list.InsertColumn(2, L"A 明文/存储", LVCFMT_RIGHT, 118);
    m_list.InsertColumn(3, L"B 明文/存储", LVCFMT_RIGHT, 118);
    m_list.InsertColumn(4, L"差异说明", LVCFMT_LEFT, 260);

    m_detail.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                    CRect(0, 0, 10, 10), this, IDC_CMP_DETAIL);
    m_lblPkgInfo.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(0, 0, 10, 10), this, IDC_CMP_PKGINFO);
    m_progress.Create(WS_CHILD | WS_VISIBLE | PBS_SMOOTH, CRect(0, 0, 10, 10), this, IDC_CMP_PROGRESS);
    m_progress.SetRange(0, 100);
    m_status.Create(L"选择两个 DPK 包后点「开始对比」。", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_SUNKEN,
                    CRect(0, 0, 10, 10), this, IDC_CMP_STATUS);

    // 统一字体：与主窗口一致（9pt，候选字体按优先级回退）
    {
        const int dpiY = ::GetDeviceCaps(GetDC()->GetSafeHdc(), LOGPIXELSY);
        const int pixelH = -MulDiv(9, dpiY, 72);
        const wchar_t* candidates[] = {
            L"Microsoft YaHei UI",
            L"Segoe UI",
            L"Microsoft YaHei",
            L"MS Shell Dlg",
        };
        for (const wchar_t* name : candidates) {
            if (m_fontMain.CreateFont(pixelH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, name)) {
                break;
            }
        }
    }
    ApplyFontToChildren();

    Layout();
    CenterWindow(GetDesktopWindow());
    return 0;
}

void CCompareWindow::ApplyFontToChildren() {
    if (m_fontMain.GetSafeHandle() == nullptr) return;
    HWND all[] = {
        m_lblA.GetSafeHwnd(),        m_edtA.GetSafeHwnd(),  m_btnBrowseA.GetSafeHwnd(),
        m_lblB.GetSafeHwnd(),        m_edtB.GetSafeHwnd(),  m_btnBrowseB.GetSafeHwnd(),
        m_lblDepth.GetSafeHwnd(),    m_cmbDepth.GetSafeHwnd(), m_lblLimit.GetSafeHwnd(),
        m_cmbLimit.GetSafeHwnd(),    m_btnRun.GetSafeHwnd(),
        m_btnExport.GetSafeHwnd(),   m_btnClose.GetSafeHwnd(), m_lblFilter.GetSafeHwnd(),
        m_cmbFilter.GetSafeHwnd(),   m_lblStats.GetSafeHwnd(), m_list.GetSafeHwnd(),
        m_detail.GetSafeHwnd(),      m_lblPkgInfo.GetSafeHwnd(), m_progress.GetSafeHwnd(),
        m_status.GetSafeHwnd(),
    };
    for (HWND h : all) {
        if (h) ::SendMessage(h, WM_SETFONT, (WPARAM)m_fontMain.GetSafeHandle(), TRUE);
    }
}

void CCompareWindow::OnGetMinMaxInfo(MINMAXINFO* lpMMI) {
    lpMMI->ptMinTrackSize.x = 780;
    lpMMI->ptMinTrackSize.y = 570;
    CFrameWnd::OnGetMinMaxInfo(lpMMI);
}

void CCompareWindow::Layout() {
    if (m_list.GetSafeHwnd() == nullptr) return;
    CRect rc;
    GetClientRect(&rc);
    const int right = rc.Width() - kMargin;

    const int row1 = kMargin;
    const int row2 = row1 + kRowH + kRowGap;
    const int row3 = row2 + kRowH + kRowGap;
    const int row4 = row3 + kRowH + kRowGap;
    const int row5 = row4 + kRowH + kRowGap;

    // 包 A / 包 B 两行：标签 + 路径编辑框（弹性） + 浏览按钮
    const int browseW = 74;
    const int lblW = 62;
    m_lblA.MoveWindow(kMargin, row1, lblW, kRowH);
    m_lblB.MoveWindow(kMargin, row2, lblW, kRowH);
    m_btnBrowseA.MoveWindow(right - browseW, row1, browseW, kRowH);
    m_btnBrowseB.MoveWindow(right - browseW, row2, browseW, kRowH);
    m_edtA.MoveWindow(kMargin + lblW, row1 + 2, (right - browseW - kRowGap) - (kMargin + lblW), kRowH - 4);
    m_edtB.MoveWindow(kMargin + lblW, row2 + 2, (right - browseW - kRowGap) - (kMargin + lblW), kRowH - 4);

    // 深度 + 操作按钮行
    int x = kMargin;
    const int lblDepthW = 74;
    m_lblDepth.MoveWindow(x, row3, lblDepthW, kRowH);
    x += lblDepthW;
    const int depthW = 320;
    m_cmbDepth.MoveWindow(x, row3, depthW, 200);
    x += depthW + kRowGap;
    m_btnRun.MoveWindow(x, row3, 100, kRowH);
    x += 100 + kRowGap;
    m_btnExport.MoveWindow(x, row3, 112, kRowH);
    x += 112 + kRowGap;
    m_btnClose.MoveWindow(x, row3, 74, kRowH);

    // 大小差异上限行
    const int lblLimitW = 122;
    m_lblLimit.MoveWindow(kMargin, row4, lblLimitW, kRowH);
    m_cmbLimit.MoveWindow(kMargin + lblLimitW, row4, 96, 200);

    // 筛选 + 统计行
    const int lblFilterW = 34;
    m_lblFilter.MoveWindow(kMargin, row5, lblFilterW, kRowH);
    const int filterW = 190;
    m_cmbFilter.MoveWindow(kMargin + lblFilterW, row5, filterW, 200);
    m_lblStats.MoveWindow(kMargin + lblFilterW + filterW + kRowGap, row5,
                          right - (kMargin + lblFilterW + filterW + kRowGap), kRowH);

    // 自底向上：状态条 / 进度 / 包信息 / 明细 / 列表
    const int listTop = row5 + kRowH + kRowGap;
    const int statusTop = rc.Height() - kStatusH;
    m_status.MoveWindow(kMargin, statusTop, right - kMargin, kStatusH);
    const int progressTop = statusTop - kProgressH - 4;
    m_progress.MoveWindow(kMargin, progressTop, right - kMargin, kProgressH);
    const int pkgTop = progressTop - kPkgInfoH - 4;
    m_lblPkgInfo.MoveWindow(kMargin, pkgTop, right - kMargin, kPkgInfoH);
    const int detailTop = pkgTop - kDetailH - kRowGap;
    m_detail.MoveWindow(kMargin, detailTop, right - kMargin, kDetailH);
    int listH = detailTop - listTop - kRowGap;
    if (listH < 80) listH = 80;
    m_list.MoveWindow(kMargin, listTop, right - kMargin, listH);

    const int pathW = (right - kMargin) - (56 + 118 + 118 + 260) - 24;
    m_list.SetColumnWidth(1, pathW < 160 ? 160 : pathW);
}

void CCompareWindow::OnSize(UINT nType, int cx, int cy) {
    CFrameWnd::OnSize(nType, cx, cy);
    if (nType != SIZE_MINIMIZED) Layout();
}

void CCompareWindow::SetStatus(const CString& text) {
    if (m_status.GetSafeHwnd() != nullptr) {
        CString one = text;
        one.Replace(L"\r\n", L" | ");
        one.Replace(L"\n", L" | ");
        if (one.GetLength() > 400) one = one.Left(400) + L"…";
        m_status.SetWindowText(one);
    }
}

bool CCompareWindow::ProgressTick(const std::wstring& message, int percent) {
    if (m_progress.GetSafeHwnd() != nullptr) {
        m_progress.SetPos(percent >= 0 && percent <= 100 ? percent : 0);
    }
    SetStatus(W(dpk::WideToUtf8(message)) + L"（Esc 取消）");
    if ((::GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
        m_cancel = true;
    }
    MSG msg;
    while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            ::PostQuitMessage((int)msg.wParam);
            m_cancel = true;
            break;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }
    return !m_cancel;
}

void CCompareWindow::OnBrowseA() {
    if (m_busy) return;
    CFileDialog dlg(TRUE, L"dpk", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                    L"DPK 包 (*.dpk)|*.dpk|所有文件 (*.*)|*.*||", this);
    dlg.m_ofn.lpstrTitle = L"选择包 A";
    if (dlg.DoModal() == IDOK) m_edtA.SetWindowText(dlg.GetPathName());
}

void CCompareWindow::OnBrowseB() {
    if (m_busy) return;
    CFileDialog dlg(TRUE, L"dpk", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                    L"DPK 包 (*.dpk)|*.dpk|所有文件 (*.*)|*.*||", this);
    dlg.m_ofn.lpstrTitle = L"选择包 B";
    if (dlg.DoModal() == IDOK) m_edtB.SetWindowText(dlg.GetPathName());
}

int CCompareWindow::ParseSizeLimit(CString& errText) {
    CString text;
    m_cmbLimit.GetWindowText(text);
    text.Trim();
    if (text.IsEmpty() || text.CompareNoCase(L"不限制") == 0 || text == L"0") {
        return -1;  // 不限制
    }
    wchar_t* end = nullptr;
    const long v = wcstol(text, &end, 10);
    if (end == nullptr || *end != L'\0' || v < 0) {
        errText = text;
        return -2;  // 无效
    }
    return (int)v;
}

void CCompareWindow::OnRun() {
    if (m_busy) return;
    CString pathA, pathB;
    m_edtA.GetWindowText(pathA);
    m_edtB.GetWindowText(pathB);
    if (pathA.IsEmpty() || pathB.IsEmpty()) {
        MessageBox(L"请先选择要对比的两个 DPK 包。", L"DPK 包对比", MB_ICONINFORMATION);
        return;
    }
    if (CString(pathA).CompareNoCase(pathB) == 0) {
        MessageBox(L"包 A 与包 B 是同一个文件，对比结果没有意义。", L"DPK 包对比", MB_ICONINFORMATION);
        return;
    }

    // 对比前的大小差异检查：两个包相差过大通常意味着选错了文件
    CString limitErr;
    const int limitMB = ParseSizeLimit(limitErr);
    if (limitMB == -2) {
        CString msg;
        msg.Format(L"「大小差异上限」填写无效: %s\r\n请填 MB 数值（如 100、200），填 0 或「不限制」表示不检查。",
                   (LPCTSTR)limitErr);
        MessageBox(msg, L"DPK 包对比", MB_ICONWARNING);
        return;
    }
    uint64_t sizeA = 0, sizeB = 0;
    if (limitMB >= 0 && dpk::FileExists(std::wstring(pathA)) && dpk::FileExists(std::wstring(pathB)) &&
        dpk::GetFileSize(std::wstring(pathA), sizeA) && dpk::GetFileSize(std::wstring(pathB), sizeB)) {
        const uint64_t diff = sizeA > sizeB ? sizeA - sizeB : sizeB - sizeA;
        const uint64_t limitBytes = (uint64_t)limitMB * 1024ull * 1024ull;
        if (diff > limitBytes) {
            CString msg;
            msg.Format(L"已停止对比：包 A 与包 B 大小差异 %s，超过上限 %d MB。\r\n\r\n包 A: %s (%s)\r\n包 B: %s (%s)"
                       L"\r\n\r\n如确认要对比，请把「大小差异上限」调大或设为「不限制」。",
                       (LPCTSTR)BytesText(diff), limitMB, (LPCTSTR)pathA, (LPCTSTR)BytesText(sizeA), (LPCTSTR)pathB,
                       (LPCTSTR)BytesText(sizeB));
            SetStatus(L"已停止对比：大小差异超过上限。");
            MessageBox(msg, L"DPK 包对比", MB_ICONWARNING);
            return;
        }
    }

    switch (m_cmbDepth.GetCurSel()) {
        case 0: m_depth = dpk::CompareDepth::PathsOnly; break;
        case 2: m_depth = dpk::CompareDepth::Full; break;
        default: m_depth = dpk::CompareDepth::Metadata; break;
    }

    m_pathA = std::wstring(pathA);
    m_pathB = std::wstring(pathB);
    BusyGuard guard(m_busy);
    m_cancel = false;
    m_progress.SetPos(0);
    std::string err;
    const bool ok = dpk::ComparePackages(
        std::wstring(pathA), std::wstring(pathB), m_depth, m_result, err,
        [this](const std::wstring& m, int p) -> bool { return ProgressTick(m, p); });
    m_progress.SetPos(ok ? 100 : 0);

    if (!ok) {
        SetStatus(W("对比失败: " + err));
        MessageBox(W("对比失败: " + err), L"DPK 包对比", MB_ICONWARNING);
        return;
    }

    m_hasResult = true;
    m_detail.SetWindowText(L"");
    RebuildView();

    const dpk::CompareStats& st = m_result.stats;
    CString stats;
    stats.Format(L"相同 %zu   不同 %zu   仅 A %zu   仅 B %zu   错误 %zu   合计 %zu%s", st.same, st.different, st.onlyA,
                 st.onlyB, st.errors, st.Total(), m_result.completed ? L"" : L"（已取消，部分结果）");
    m_lblStats.SetWindowText(stats);

    CString pkg;
    pkg.Format(L"A: %s · %u 块 · %zu 条 · treeRoot=%u namesRoot=%u\r\nB: %s · %u 块 · %zu 条 · treeRoot=%u namesRoot=%u",
               (LPCTSTR)BytesText(m_result.pkgA.fileSize), m_result.pkgA.totalBlocks, m_result.pkgA.entryCount,
               m_result.pkgA.treeRoot, m_result.pkgA.namesRoot, (LPCTSTR)BytesText(m_result.pkgB.fileSize),
               m_result.pkgB.totalBlocks, m_result.pkgB.entryCount, m_result.pkgB.treeRoot, m_result.pkgB.namesRoot);
    m_lblPkgInfo.SetWindowText(pkg);

    const wchar_t* depthName = m_depth == dpk::CompareDepth::PathsOnly ? L"仅路径"
                               : m_depth == dpk::CompareDepth::Full    ? L"完整"
                                                                        : L"标准";
    CString done;
    done.Format(L"对比%s（%s深度）：共 %zu 条，不同 %zu，仅 A %zu，仅 B %zu，错误 %zu。双击「不同」条目可查看内容对比。",
                m_result.completed ? L"完成" : L"已取消", depthName, st.Total(), st.different, st.onlyA, st.onlyB,
                st.errors);
    SetStatus(done);
}

void CCompareWindow::OnEntryDblClk(NMHDR* pNMHDR, LRESULT* pResult) {
    const NMITEMACTIVATE* info = reinterpret_cast<const NMITEMACTIVATE*>(pNMHDR);
    *pResult = 0;
    const int row = info ? info->iItem : -1;
    if (m_busy || row < 0 || row >= (int)m_view.size()) return;
    if (!m_hasResult || !m_result.completed) return;
    const dpk::CompareEntry& e = m_result.entries[(size_t)m_view[row]];
    if (e.status != dpk::CompareStatus::Different) {
        SetStatus(L"仅「不同」状态的条目支持双击对比内容。");
        return;
    }
    CWaitCursor wait;
    CDiffViewer* viewer = new CDiffViewer();
    if (!viewer->Create(m_pathA, m_pathB, e.pathUtf8)) {
        delete viewer;
        MessageBox(L"创建内容对比窗口失败。", L"DPK 包对比", MB_ICONWARNING);
        return;
    }
    viewer->ShowWindow(SW_SHOW);
    viewer->UpdateWindow();
}

void CCompareWindow::RebuildView() {
    if (!m_hasResult) return;
    m_ignoreSel = true;
    m_list.DeleteAllItems();
    m_view.clear();

    const int filter = m_cmbFilter.GetCurSel();  // 0 全部 1 仅差异 2 仅内容不同 3 仅A 4 仅B 5 仅相同
    const std::vector<dpk::CompareEntry>& entries = m_result.entries;
    for (size_t i = 0; i < entries.size(); ++i) {
        const dpk::CompareStatus st = entries[i].status;
        bool keep = true;
        switch (filter) {
            case 1: keep = st != dpk::CompareStatus::Same; break;
            case 2: keep = st == dpk::CompareStatus::Different && !entries[i].contentSame; break;
            case 3: keep = st == dpk::CompareStatus::OnlyA; break;
            case 4: keep = st == dpk::CompareStatus::OnlyB; break;
            case 5: keep = st == dpk::CompareStatus::Same; break;
            default: break;
        }
        if (keep) m_view.push_back((int)i);
    }
    m_list.SetItemCountEx((int)m_view.size(), LVSICF_NOSCROLL);
    m_ignoreSel = false;
}

void CCompareWindow::OnFilterChanged() {
    if (m_busy) return;
    RebuildView();
}

void CCompareWindow::OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult) {
    NMLVDISPINFO* pdi = (NMLVDISPINFO*)pNMHDR;
    LVITEM* pItem = &pdi->item;
    if (!(pItem->mask & LVIF_TEXT)) {
        *pResult = 0;
        return;
    }
    const int row = pItem->iItem;
    if (row < 0 || row >= (int)m_view.size()) {
        *pResult = 0;
        return;
    }
    const dpk::CompareEntry& e = m_result.entries[(size_t)m_view[row]];
    CString buf;
    switch (pItem->iSubItem) {
        case 0: buf = StatusText(e.status); break;
        case 1: buf = W(e.pathUtf8); break;
        case 2: buf = SideSizeText(e.a); break;
        case 3: buf = SideSizeText(e.b); break;
        case 4: buf = e.note.empty() ? CString(L"—") : W(e.note); break;
        default: buf = L""; break;
    }
    if (pItem->cchTextMax > 0) {
        _tcsncpy_s(pItem->pszText, pItem->cchTextMax, buf, _TRUNCATE);
    }
    *pResult = 0;
}

void CCompareWindow::OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult) {
    NMLVCUSTOMDRAW* pcd = (NMLVCUSTOMDRAW*)pNMHDR;
    switch (pcd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            *pResult = CDRF_NOTIFYITEMDRAW;
            return;
        case CDDS_ITEMPREPAINT:
            *pResult = CDRF_NOTIFYSUBITEMDRAW;
            return;
        case CDDS_SUBITEM | CDDS_ITEMPREPAINT: {
            // 状态列按类别着色，其余列保持默认
            if (pcd->iSubItem == 0) {
                const int row = (int)pcd->nmcd.dwItemSpec;
                if (row >= 0 && row < (int)m_view.size()) {
                    pcd->clrText = StatusColor(m_result.entries[(size_t)m_view[row]].status);
                }
            }
            *pResult = CDRF_DODEFAULT;
            return;
        }
        default:
            *pResult = CDRF_DODEFAULT;
            return;
    }
}

void CCompareWindow::OnEntryChanged(NMHDR* pNMHDR, LRESULT* pResult) {
    LPNMLISTVIEW info = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
    if (!m_ignoreSel && (info->uChanged & LVIF_STATE) != 0 && (info->uNewState & LVIS_SELECTED) != 0) {
        ShowDetail(info->iItem);
    }
    *pResult = 0;
}

void CCompareWindow::ShowDetail(int row) {
    if (!m_hasResult || row < 0 || row >= (int)m_view.size()) {
        m_detail.SetWindowText(L"");
        return;
    }
    const dpk::CompareEntry& e = m_result.entries[(size_t)m_view[row]];

    auto sideLine = [&](const wchar_t* tag, const dpk::CompareSideInfo& side) -> CString {
        if (!side.present) {
            CString s;
            s.Format(L"%s: 无此条目\r\n", tag);
            return s;
        }
        if (side.headerOk || side.decoded) {
            CString s;
            s.Format(L"%s: %s  明文 %s  存储 %s  根块 %u", tag, (LPCTSTR)FlagsText(side.header),
                     (LPCTSTR)BytesText(side.header.plainSize), (LPCTSTR)BytesText(side.header.storedSize),
                     side.rootBlock);
            if (side.headerOk && !side.decoded) s += L"  MD5 " + Md5HeadText(side.md5);
            return s + L"\r\n";
        }
        CString s;
        s.Format(L"%s: 读取失败 - %s\r\n", tag, (LPCTSTR)W(side.err));
        return s;
    };

    CString text;
    text += L"路径: " + W(e.pathUtf8) + L"\r\n";
    text += sideLine(L"A", e.a);
    text += sideLine(L"B", e.b);
    text += L"判定: " + StatusText(e.status);
    if (!e.note.empty()) text += L" —— " + W(e.note);
    m_detail.SetWindowText(text);
}

void CCompareWindow::OnExport() {
    if (m_busy) return;
    if (!m_hasResult) {
        MessageBox(L"还没有对比结果，请先执行一次对比。", L"DPK 包对比", MB_ICONINFORMATION);
        return;
    }
    CFileDialog dlg(FALSE, L"txt", L"dpk_compare_report.txt", OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
                    L"文本文件 (*.txt)|*.txt|所有文件 (*.*)|*.*||", this);
    dlg.m_ofn.lpstrTitle = L"导出对比报告";
    if (dlg.DoModal() != IDOK) return;

    std::string report = dpk::CompareReportText(m_result, m_depth);
    report.insert(report.begin(), (char)0xEF);
    report.insert(report.begin() + 1, (char)0xBB);
    report.insert(report.begin() + 2, (char)0xBF);
    std::string err;
    if (!dpk::WriteFileBytes(std::wstring(dlg.GetPathName()), (const uint8_t*)report.data(), report.size(), err)) {
        MessageBox(W("写出报告失败: " + err), L"DPK 包对比", MB_ICONWARNING);
        return;
    }
    CString done;
    done.Format(L"报告已导出: %s", (LPCTSTR)dlg.GetPathName());
    SetStatus(done);
}

void CCompareWindow::OnClose() {
    if (m_busy) {
        // 对比进行中：先请求取消，本轮结束后再关闭
        m_cancel = true;
        return;
    }
    DestroyWindow();
}

void CCompareWindow::PostNcDestroy() {
    if (CWnd* parent = GetParent()) {
        parent->PostMessage(kWMCompareClosed, 0, 0);
    }
    delete this;
}
