// MainFrame.cpp - DPKViewer 主窗口实现：浏览 / 解包 / 生成 三条功能链路
#include <afxwin.h>
#include <afxcmn.h>
#include <afxdlgs.h>

#include <algorithm>
#include <map>
#include <set>

#include "MainFrame.h"
#include "AppUtil.h"
#include "CompareWindow.h"
#include "FileViewer.h"
#include "resource.h"

#include "Core/DpkReader.h"
#include "Core/DpkWriter.h"
#include "Core/FileIO.h"
#include "Core/Md5.h"
#include "Core/StrUtil.h"
#include "Core/Whsc.h"

#pragma comment(lib, "comctl32.lib")

namespace {

const int kMargin = 8;
const int kRowH = 26;
const int kRowGap = 6;
const int kDetailH = 112;
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

// 运行时把「正在执行」标志置位，避免进度泵消息时的重入
struct BusyGuard {
    explicit BusyGuard(bool& flag) : flag_(flag) { flag_ = true; }
    ~BusyGuard() { flag_ = false; }
    bool& flag_;
};

}  // namespace

IMPLEMENT_DYNAMIC(CMainFrame, CFrameWnd)

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_COMMAND(ID_APP_OPEN, &CMainFrame::OnOpenPackage)
    ON_COMMAND(ID_APP_EXTRACT, &CMainFrame::OnExtractAll)
    ON_COMMAND(ID_APP_PACK, &CMainFrame::OnPackFolder)
    ON_COMMAND(ID_APP_COMPARE, &CMainFrame::OnComparePackages)
    ON_COMMAND(ID_APP_USAGE, &CMainFrame::OnUsage)
    ON_COMMAND(ID_APP_ABOUT, &CMainFrame::OnAbout)
    ON_COMMAND(ID_APP_EXIT, &CMainFrame::OnAppExit)
    ON_BN_CLICKED(IDC_BTN_OPEN, &CMainFrame::OnOpenPackage)
    ON_BN_CLICKED(IDC_BTN_EXTRACT, &CMainFrame::OnExtractAll)
    ON_BN_CLICKED(IDC_BTN_PACK, &CMainFrame::OnPackFolder)
    ON_BN_CLICKED(IDC_BTN_USAGE, &CMainFrame::OnUsage)
    ON_BN_CLICKED(IDC_BTN_ABOUT, &CMainFrame::OnAbout)
    ON_EN_CHANGE(IDC_EDIT_FILTER, &CMainFrame::OnFilterChanged)
    ON_BN_CLICKED(IDC_CHK_FOLDER, &CMainFrame::OnFolderViewChanged)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_ENTRY_LIST, &CMainFrame::OnEntryChanged)
    ON_NOTIFY(NM_DBLCLK, IDC_ENTRY_LIST, &CMainFrame::OnListDblClk)
    ON_NOTIFY(NM_RCLICK, IDC_ENTRY_LIST, &CMainFrame::OnListRightClick)
    ON_COMMAND(ID_CTX_VIEW_FILE, &CMainFrame::OnCtxViewFile)
    ON_COMMAND(ID_CTX_EXPORT_FILE, &CMainFrame::OnCtxExportFile)
    ON_COMMAND(ID_CTX_EXPORT_FOLDER, &CMainFrame::OnCtxExportFolder)
    ON_COMMAND(ID_CTX_IMPORT_FILE, &CMainFrame::OnCtxImportFile)
    ON_NOTIFY(LVN_GETDISPINFO, IDC_ENTRY_LIST, &CMainFrame::OnGetdispinfo)
    ON_MESSAGE(kWMCompareClosed, &CMainFrame::OnCompareClosed)
END_MESSAGE_MAP()

CMainFrame::CMainFrame() {}

CMainFrame::~CMainFrame() {}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1) return -1;

    SetIcon(::LoadIcon(::GetModuleHandle(nullptr), MAKEINTRESOURCE(IDR_MAINFRAME)), TRUE);

    if (!m_btnOpen.Create(L"打开包(&O)…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this,
                          IDC_BTN_OPEN)) {
        return -1;
    }
    m_btnExtract.Create(L"解包到目录(&E)…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this,
                        IDC_BTN_EXTRACT);
    m_btnPack.Create(L"生成 DPK 包(&P)…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this,
                     IDC_BTN_PACK);
    m_btnUsage.Create(L"使用说明(&U)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this,
                      IDC_BTN_USAGE);
    m_btnAbout.Create(L"关于(&A)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this, IDC_BTN_ABOUT);

    m_lblFilter.Create(L"筛选:", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, CRect(0, 0, 10, 10), this,
                       IDC_LBL_FILTER);
    m_edtFilter.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, CRect(0, 0, 10, 10), this,
                       IDC_EDIT_FILTER);

    m_chkCompress.Create(L"压缩(L9)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, CRect(0, 0, 10, 10), this,
                         IDC_CHK_COMPRESS);
    m_chkEncrypt.Create(L"加密", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, CRect(0, 0, 10, 10), this, IDC_CHK_ENCRYPT);
    m_chkTemplate.Create(L"沿用已打开包的包头", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, CRect(0, 0, 10, 10), this,
                         IDC_CHK_TEMPLATE);
    m_chkFolder.Create(L"文件夹收纳(&F)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, CRect(0, 0, 10, 10), this,
                       IDC_CHK_FOLDER);
    m_chkCompress.SetCheck(BST_CHECKED);
    m_chkEncrypt.SetCheck(BST_CHECKED);
    m_chkFolder.SetCheck(BST_CHECKED);

    m_list.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER |
                      LVS_OWNERDATA,
                  CRect(0, 0, 10, 10), this, IDC_ENTRY_LIST);
    m_list.SetExtendedStyle(m_list.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    m_list.InsertColumn(0, L"条目路径", LVCFMT_LEFT, 360);
    m_list.InsertColumn(1, L"根块", LVCFMT_RIGHT, 70);
    m_list.InsertColumn(2, L"块数", LVCFMT_RIGHT, 60);
    m_list.InsertColumn(3, L"明文", LVCFMT_RIGHT, 100);
    m_list.InsertColumn(4, L"存储", LVCFMT_RIGHT, 100);
    m_list.InsertColumn(5, L"标志", LVCFMT_LEFT, 170);
    m_list.InsertColumn(6, L"校验", LVCFMT_LEFT, 90);

    m_detail.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                    CRect(0, 0, 10, 10), this, IDC_DETAIL);
    m_progress.Create(WS_CHILD | WS_VISIBLE | PBS_SMOOTH, CRect(0, 0, 10, 10), this, IDC_PROGRESS);
    m_progress.SetRange(0, 100);

    m_status.Create(L"就绪：请打开一个 DPK 包（扩展名 .dpk），或使用 /list /extract /pack 命令行模式。",
                    WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_SUNKEN, CRect(0, 0, 10, 10), this,
                    IDC_STATUS);

    CRect rc;
    GetClientRect(&rc);
    Layout();

    // 统一字体：9pt，ClearType 渲染，候选字体按优先级回退
    // 字高用负像素高度锁定实际显示大小，避免不同 DPI 下漂移
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
    if (m_fontMain.GetSafeHandle() != nullptr) {
        HWND all[] = {
            m_btnOpen.GetSafeHwnd(),
            m_btnExtract.GetSafeHwnd(),
            m_btnPack.GetSafeHwnd(),
            m_btnUsage.GetSafeHwnd(),
            m_btnAbout.GetSafeHwnd(),
            m_lblFilter.GetSafeHwnd(),
            m_edtFilter.GetSafeHwnd(),
            m_chkCompress.GetSafeHwnd(),
            m_chkEncrypt.GetSafeHwnd(),
            m_chkTemplate.GetSafeHwnd(),
            m_chkFolder.GetSafeHwnd(),
            m_list.GetSafeHwnd(),
            m_detail.GetSafeHwnd(),
            m_progress.GetSafeHwnd(),
            m_status.GetSafeHwnd(),
        };
        for (HWND h : all) {
            if (h) ::SendMessage(h, WM_SETFONT, (WPARAM)m_fontMain.GetSafeHandle(), TRUE);
        }
    }

    return 0;
}

void CMainFrame::OnGetMinMaxInfo(MINMAXINFO* lpMMI) {
    lpMMI->ptMinTrackSize.x = 860;
    lpMMI->ptMinTrackSize.y = 560;
    CFrameWnd::OnGetMinMaxInfo(lpMMI);
}

void CMainFrame::Layout() {
    if (m_list.GetSafeHwnd() == nullptr) return;
    CRect rc;
    GetClientRect(&rc);
    const int right = rc.Width() - kMargin;

    const int row1 = kMargin;
    const int row2 = row1 + kRowH + kRowGap;

    int x = kMargin;
    m_btnOpen.MoveWindow(x, row1, 118, kRowH);
    x += 118 + kRowGap;
    m_btnExtract.MoveWindow(x, row1, 138, kRowH);
    x += 138 + kRowGap;
    m_btnPack.MoveWindow(x, row1, 138, kRowH);
    x += 138 + kRowGap;
    m_btnUsage.MoveWindow(x, row1, 100, kRowH);
    x += 100 + kRowGap;
    m_btnAbout.MoveWindow(x, row1, 90, kRowH);

    int fx = kMargin;
    m_lblFilter.MoveWindow(fx, row2, 40, kRowH);
    fx += 40;
    const int filterW = 220;
    m_edtFilter.MoveWindow(fx, row2 + 2, filterW, kRowH - 4);
    fx += filterW + kRowGap;
    m_chkCompress.MoveWindow(fx, row2, 90, kRowH);
    fx += 90 + kRowGap;
    m_chkEncrypt.MoveWindow(fx, row2, 70, kRowH);
    fx += 70 + kRowGap;
    m_chkTemplate.MoveWindow(fx, row2, 170, kRowH);
    fx += 170 + kRowGap;
    m_chkFolder.MoveWindow(fx, row2, 96, kRowH);

    const int listTop = row2 + kRowH + kRowGap;
    int statusTop = rc.Height() - kStatusH;
    m_status.MoveWindow(kMargin, statusTop, right - kMargin, kStatusH);
    const int progressTop = statusTop - kProgressH - 4;
    m_progress.MoveWindow(kMargin, progressTop, right - kMargin, kProgressH);
    const int detailTop = progressTop - kDetailH - kRowGap;
    m_detail.MoveWindow(kMargin, detailTop, right - kMargin, kDetailH);
    int listH = detailTop - listTop - kRowGap;
    if (listH < 80) listH = 80;
    m_list.MoveWindow(kMargin, listTop, right - kMargin, listH);

    const int pathW = (right - kMargin) - (70 + 60 + 100 + 100 + 170 + 90) - (m_list.GetSafeHwnd() ? 24 : 0);
    m_list.SetColumnWidth(0, pathW < 160 ? 160 : pathW);
}

void CMainFrame::OnSize(UINT nType, int cx, int cy) {
    CFrameWnd::OnSize(nType, cx, cy);
    if (nType != SIZE_MINIMIZED) Layout();
}

void CMainFrame::SetStatus(const CString& text) {
    if (m_status.GetSafeHwnd() != nullptr) {
        CString one = text;
        one.Replace(L"\r\n", L" | ");
        one.Replace(L"\n", L" | ");
        if (one.GetLength() > 400) one = one.Left(400) + L"…";
        m_status.SetWindowText(one);
    }
}

bool CMainFrame::ProgressTick(const std::wstring& message, int percent) {
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

bool CMainFrame::OpenPackage(const std::wstring& path) {
    std::string err;
    if (!m_reader.Open(path, err)) {
        MessageBox(W("打开包失败: " + err), L"DPKViewer", MB_ICONWARNING);
        return false;
    }
    m_curFolder.clear();  // 新包从根目录开始浏览
    // computeBlockCounts=false：块数是 O(条目数 × 平均块数) 的遍历，大包要几分钟
    // 列表页先不显示块数，选中条目时 ShowDetail 里按需 CountChain
    if (!m_reader.LoadEntries(err, false)) {
        MessageBox(W("解析包目录失败: " + err), L"DPKViewer", MB_ICONWARNING);
        m_reader.Close();
        return false;
    }
    RefreshList();
    CString info;
    info.Format(L"%s | 大小 %s | 总块数 %u | 条目 %u 条 | treeRoot=%u namesRoot=%u", (LPCTSTR)W(dpk::WideToUtf8(path)),
                (LPCTSTR)BytesText(m_reader.FileSize()), m_reader.TotalBlocks(), (unsigned)m_reader.Entries().size(),
                m_reader.TreeRoot(), m_reader.NamesRoot());
    SetStatus(info);
    return true;
}

void CMainFrame::OnOpenPackage() {
    if (m_busy) return;
    CFileDialog dlg(TRUE, L"dpk", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                    L"DPK 包 (*.dpk)|*.dpk|所有文件 (*.*)|*.*||", this);
    dlg.m_ofn.lpstrTitle = L"打开 DPK 包";
    if (dlg.DoModal() != IDOK) return;
    OpenPackage(std::wstring(dlg.GetPathName()));
}

void CMainFrame::RefreshList() {
    m_ignoreSel = true;
    m_list.DeleteAllItems();
    m_view.clear();
    const std::vector<dpk::Entry>& entries = m_reader.Entries();
    m_meta.assign(entries.size(), EntryMeta());

    CString filter;
    m_edtFilter.GetWindowText(filter);
    const std::string filterU8 = dpk::ToLowerAscii(dpk::WideToUtf8(std::wstring(filter)));

    // 只构建行映射，不做任何 I/O
    // LVS_OWNERDATA 虚拟列表：SetItemCountEx 后由 LVN_GETDISPINFO 按需填充
    if (m_folderMode) {
        if (!filterU8.empty()) {
            // 筛选时全局搜索整个包：显示所有路径匹配的条目（完整路径），不限于当前文件夹
            for (size_t i = 0; i < entries.size(); ++i) {
                const std::string lower = dpk::ToLowerAscii(entries[i].pathUtf8);
                if (lower.find(filterU8) == std::string::npos) continue;
                ListRow row;
                row.entryIdx = (int)i;
                row.name = dpk::Utf8ToWide(entries[i].pathUtf8);  // 名称列显示完整路径
                m_view.push_back(row);
            }
            m_list.SetItemCountEx((int)m_view.size(), LVSICF_NOSCROLL);
            m_ignoreSel = false;
            CString status;
            status.Format(L"筛选「%s」（全包范围）：匹配 %zu 条（共 %u 条目）。清空筛选返回文件夹浏览。",
                          (LPCTSTR)filter, m_view.size(), (unsigned)entries.size());
            SetStatus(status);
            return;
        }
        // 文件夹收纳视图：当前文件夹下的子文件夹在前、文件在后，按名称排序
        const std::string prefixU8 =
            m_curFolder.empty() ? std::string() : dpk::WideToUtf8(m_curFolder) + "/";
        std::vector<std::pair<std::wstring, int>> folderRows, fileRows;
        for (size_t i = 0; i < entries.size(); ++i) {
            const std::string& p = entries[i].pathUtf8;
            if (!prefixU8.empty() &&
                (p.size() <= prefixU8.size() || p.compare(0, prefixU8.size(), prefixU8) != 0)) {
                continue;
            }
            const std::string rest = p.substr(prefixU8.size());
            const size_t slash = rest.find('/');
            if (slash == std::string::npos) {
                fileRows.emplace_back(dpk::Utf8ToWide(rest), (int)i);
            } else {
                folderRows.emplace_back(dpk::Utf8ToWide(rest.substr(0, slash)), -1);
            }
        }
        auto byName = [](const std::pair<std::wstring, int>& a, const std::pair<std::wstring, int>& b) {
            return dpk::ToLowerAscii(dpk::WideToUtf8(a.first)) < dpk::ToLowerAscii(dpk::WideToUtf8(b.first));
        };
        // 文件夹去重（同名子目录只显示一次）
        std::sort(folderRows.begin(), folderRows.end(), byName);
        folderRows.erase(std::unique(folderRows.begin(), folderRows.end(),
                                     [](const std::pair<std::wstring, int>& a, const std::pair<std::wstring, int>& b) {
                                         return a.first == b.first;
                                     }),
                         folderRows.end());
        std::sort(fileRows.begin(), fileRows.end(), byName);

        if (!m_curFolder.empty()) {
            ListRow row;
            row.isParent = true;
            m_view.push_back(row);
        }
        for (const auto& f : folderRows) {
            ListRow row;
            row.name = f.first;
            m_view.push_back(row);
        }
        for (const auto& f : fileRows) {
            ListRow row;
            row.entryIdx = f.second;
            row.name = f.first;
            m_view.push_back(row);
        }
    } else {
        // 平铺视图：全路径列表，筛选按路径子串
        for (size_t i = 0; i < entries.size(); ++i) {
            if (!filterU8.empty()) {
                const std::string lower = dpk::ToLowerAscii(entries[i].pathUtf8);
                if (lower.find(filterU8) == std::string::npos) continue;
            }
            ListRow row;
            row.entryIdx = (int)i;
            m_view.push_back(row);
        }
    }
    m_list.SetItemCountEx((int)m_view.size(), LVSICF_NOSCROLL);

    m_ignoreSel = false;
    CString status;
    if (m_folderMode) {
        const size_t nFolders =
            std::count_if(m_view.begin(), m_view.end(), [](const ListRow& r) { return r.entryIdx < 0 && !r.isParent; });
        const size_t nFiles =
            std::count_if(m_view.begin(), m_view.end(), [](const ListRow& r) { return r.entryIdx >= 0; });
        status.Format(L"文件夹: %s | 子文件夹 %zu 个，文件 %zu 条（共 %u 条目）。筛选可全包搜索，双击文件夹进入。",
                      m_curFolder.empty() ? L"（根目录）" : (LPCTSTR)m_curFolder.c_str(), nFolders, nFiles,
                      (unsigned)entries.size());
    } else {
        status.Format(L"条目 %u 条，当前显示 %zu 条。", (unsigned)entries.size(), m_view.size());
    }
    SetStatus(status);
}

// 虚拟列表回调：系统需要某行某列显示什么文字时才会进来
// 每行进来一次，PeekRootBlock 懒做 + 结果缓存到 m_meta，滚动回来不重读
void CMainFrame::OnListRightClick(NMHDR* pNMHDR, LRESULT* pResult) {
    *pResult = 0;
    if (!m_reader.IsOpen() || !m_reader.EntriesLoaded()) return;

    // 光标位置命中测试，拿到右键所在的行
    CPoint pt;
    ::GetCursorPos(&pt);
    CPoint local = pt;
    m_list.ScreenToClient(&local);
    const int row = m_list.HitTest(local);
    if (row < 0 || row >= (int)m_view.size()) return;
    const ListRow& listRow = m_view[(size_t)row];
    if (listRow.isParent) return;  // 「..」行不提供菜单

    // 选中该行（右键默认不改选中态），同时触发明细显示
    m_ignoreSel = true;
    m_list.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    m_ignoreSel = false;
    ShowDetail(row);
    m_ctxRow = row;

    const HMENU menu = ::CreatePopupMenu();
    if (listRow.entryIdx >= 0) {
        ::AppendMenu(menu, MF_STRING, ID_CTX_VIEW_FILE, L"查看(&V)…");
        ::AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
        ::AppendMenu(menu, MF_STRING, ID_CTX_IMPORT_FILE, L"导入文件到此处(&I)…");
        ::AppendMenu(menu, MF_STRING, ID_CTX_EXPORT_FILE, L"导出文件(&E)…");
    } else {
        ::AppendMenu(menu, MF_STRING, ID_CTX_IMPORT_FILE, L"导入文件到此处(&I)…");
        ::AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
        ::AppendMenu(menu, MF_STRING, ID_CTX_EXPORT_FOLDER, L"导出文件夹(&D)…");
    }
    ::TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, nullptr);
    ::DestroyMenu(menu);
}

void CMainFrame::OnCtxViewFile() {
    ViewEntryAtRow(m_ctxRow);
}

void CMainFrame::ViewEntryAtRow(int row) {
    if (m_busy || row < 0 || row >= (int)m_view.size()) return;
    const ListRow& listRow = m_view[(size_t)row];
    if (listRow.entryIdx < 0) return;
    const dpk::Entry& entry = m_reader.Entries()[(size_t)listRow.entryIdx];

    BusyGuard guard(m_busy);
    m_cancel = false;
    CWaitCursor wait;
    std::vector<uint8_t> plain;
    dpk::WhscInfo info;
    std::string err;
    if (!m_reader.ReadEntry(entry, plain, info, err)) {
        MessageBox(W("读取失败: " + err), L"DPKViewer - 查看", MB_ICONWARNING);
        return;
    }
    // 校验状态写回 meta，列表第 6 列同步刷新
    EntryMeta& meta = m_meta[(size_t)listRow.entryIdx];
    meta.verifyState = info.md5Ok ? 1 : 0;
    CRect rc;
    m_list.GetItemRect(row, &rc, LVIR_BOUNDS);
    m_list.InvalidateRect(&rc);

    CFileViewer* viewer = new CFileViewer();
    if (!viewer->Create(plain, entry.pathUtf8)) {
        delete viewer;
        MessageBox(L"创建查看窗口失败。", L"DPKViewer", MB_ICONWARNING);
        return;
    }
    viewer->ShowWindow(SW_SHOW);
    viewer->UpdateWindow();
}

void CMainFrame::OnCtxExportFile() {
    if (m_busy || m_ctxRow < 0 || m_ctxRow >= (int)m_view.size()) return;
    const ListRow& listRow = m_view[(size_t)m_ctxRow];
    if (listRow.entryIdx < 0) return;
    const dpk::Entry& entry = m_reader.Entries()[(size_t)listRow.entryIdx];

    // 默认文件名取条目自身的文件名
    const std::wstring full = dpk::Utf8ToWide(entry.pathUtf8);
    const std::wstring defName = dpk::FileNameOf(full);
    CFileDialog dlg(FALSE, nullptr, defName.c_str(), OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
                    L"所有文件 (*.*)|*.*||", this);
    dlg.m_ofn.lpstrTitle = L"导出文件到";
    if (dlg.DoModal() != IDOK) return;

    BusyGuard guard(m_busy);
    m_cancel = false;
    std::string err;
    if (m_reader.ExtractEntry(entry, std::wstring(dlg.GetPathName()), err)) {
        SetStatus(L"已导出: " + CString(dlg.GetPathName()));
        MessageBox(L"导出成功。\r\n" + CString(dlg.GetPathName()), L"DPKViewer - 导出文件", MB_ICONINFORMATION);
    } else {
        SetStatus(W("导出失败: " + err));
        MessageBox(W("导出失败: " + err), L"DPKViewer - 导出文件", MB_ICONWARNING);
    }
}

void CMainFrame::OnCtxExportFolder() {
    if (m_busy || m_ctxRow < 0 || m_ctxRow >= (int)m_view.size()) return;
    const ListRow& listRow = m_view[(size_t)m_ctxRow];
    if (listRow.entryIdx >= 0) return;
    const std::wstring folder =
        m_curFolder.empty() ? listRow.name : m_curFolder + L"/" + listRow.name;
    const std::string prefix = dpk::WideToUtf8(folder) + "/";

    std::wstring outDir;
    if (!app::PickFolder(m_hWnd, L"选择文件夹导出目录", outDir)) return;

    BusyGuard guard(m_busy);
    m_cancel = false;
    m_progress.SetPos(0);
    int okCount = 0;
    int failCount = 0;
    std::vector<CString> failures;
    const std::vector<dpk::Entry>& entries = m_reader.Entries();
    size_t total = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].pathUtf8.compare(0, prefix.size(), prefix) == 0) ++total;
    }
    size_t done = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        const dpk::Entry& e = entries[i];
        if (e.pathUtf8.compare(0, prefix.size(), prefix) != 0) continue;
        ++done;
        if (!ProgressTick(dpk::Utf8ToWide(e.pathUtf8), total ? (int)(done * 100 / total) : 100)) break;
        std::string eerr;
        if (!dpk::IsSafeRelativePath(e.pathUtf8)) {
            ++failCount;
            if (failures.size() < 20) failures.push_back(W(e.pathUtf8) + L" - 路径不安全，已跳过");
            continue;
        }
        const std::wstring rel = dpk::RepoRelativeToWin(dpk::Utf8ToWide(e.pathUtf8));
        if (m_reader.ExtractEntry(e, dpk::JoinPath(outDir, rel), eerr)) {
            ++okCount;
        } else {
            ++failCount;
            if (failures.size() < 20) failures.push_back(W(e.pathUtf8) + L" - " + W(eerr));
        }
    }
    m_progress.SetPos(100);

    CString summary;
    summary.Format(L"导出文件夹 %s %s：成功 %d 条，失败 %d 条。\r\n输出目录: %s",
                   (LPCTSTR)CString(folder.c_str()), m_cancel ? L"（已取消）" : L"完成", okCount, failCount,
                   (LPCTSTR)CString(outDir.c_str()));
    for (size_t i = 0; i < failures.size(); ++i) summary += L"\r\n  " + failures[i];
    CString done2;
    done2.Format(L"文件夹导出%s：成功 %d 条，失败 %d 条 -> %s", m_cancel ? L"已取消" : L"完成", okCount, failCount,
                 (LPCTSTR)CString(outDir.c_str()));
    SetStatus(done2);
    MessageBox(summary, L"DPKViewer - 导出文件夹", failCount ? MB_ICONWARNING : MB_ICONINFORMATION);
}

void CMainFrame::OnCtxImportFile() {
    if (m_busy || !m_reader.IsOpen() || !m_reader.EntriesLoaded()) return;
    if (m_ctxRow < 0 || m_ctxRow >= (int)m_view.size()) return;
    const ListRow& listRow = m_view[(size_t)m_ctxRow];

    // 导入目标目录：文件行 -> 该文件所在目录；文件夹行 -> 该文件夹
    std::wstring targetFolder;
    if (listRow.entryIdx >= 0) {
        const std::wstring full = dpk::Utf8ToWide(m_reader.Entries()[(size_t)listRow.entryIdx].pathUtf8);
        const size_t slash = full.rfind(L'/');
        targetFolder = slash == std::wstring::npos ? std::wstring() : full.substr(0, slash);
    } else if (!listRow.isParent) {
        targetFolder = m_curFolder.empty() ? listRow.name : m_curFolder + L"/" + listRow.name;
    } else {
        return;
    }

    CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, L"所有文件 (*.*)|*.*||", this);
    dlg.m_ofn.lpstrTitle = L"选择要导入的文件";
    if (dlg.DoModal() != IDOK) return;
    const std::wstring srcPath(dlg.GetPathName());
    const std::wstring fileName = dpk::FileNameOf(srcPath);
    const std::wstring relPath = targetFolder.empty() ? fileName : targetFolder + L"/" + fileName;
    const std::string relU8 = dpk::WideToUtf8(relPath);
    if (!dpk::IsSafeRelativePath(relU8)) {
        MessageBox(L"导入路径不安全（含 .. 或绝对路径），已取消。", L"DPKViewer - 导入", MB_ICONWARNING);
        return;
    }

    BusyGuard guard(m_busy);
    m_cancel = false;
    m_progress.SetPos(0);
    CWaitCursor wait;

    // 读入要导入的文件
    std::vector<uint8_t> importData;
    std::string ierr;
    if (!dpk::ReadFileBytes(srcPath, importData, ierr)) {
        MessageBox(W("读取导入文件失败: " + ierr), L"DPKViewer - 导入", MB_ICONWARNING);
        return;
    }

    // 原位补丁：只改写该条目的块（同名替换时目录树都不用动），其余条目字节不动。
    // 大包友好：不解码无关条目、不缓存整包，内存占用 ≈ 导入文件大小。
    const std::wstring pkgPath = m_reader.Path();
    // 先释放主窗口的只读句柄：fopen_s 打开的文件不可共享写入，写阶段需要独占 r+b
    m_reader.Close();
    uint32_t entryCount = 0;
    uint32_t totalBlocks = 0;
    bool replaced = false;
    std::string perr;
    const bool ok = dpk::PatchEntryInPackage(
        pkgPath, relU8, importData, m_chkCompress.GetCheck() == BST_CHECKED,
        m_chkEncrypt.GetCheck() == BST_CHECKED, 9, entryCount, totalBlocks, replaced, perr,
        [this](const std::wstring& m, int p) -> bool { return ProgressTick(m, p); });
    m_progress.SetPos(ok ? 100 : 0);
    if (!ok) {
        OpenPackage(pkgPath);  // 失败也要恢复浏览（包未被改动）
        MessageBox(W("导入失败: " + perr), L"DPKViewer - 导入", MB_ICONWARNING);
        SetStatus(m_cancel ? CString(L"导入已取消。") : W("导入失败: " + perr));
        return;
    }

    OpenPackage(pkgPath);
    CString summary;
    summary.Format(L"导入%s：%s -> %s\r\n共 %u 条，总块数 %u。其余条目原样保留。\r\n包: %s",
                   replaced ? L"（替换）" : L"（新增）", (LPCTSTR)CString(srcPath.c_str()),
                   (LPCTSTR)CString(relPath.c_str()), entryCount, totalBlocks, (LPCTSTR)CString(pkgPath.c_str()));
    SetStatus(summary);
    MessageBox(summary, L"DPKViewer - 导入", MB_ICONINFORMATION);
}

void CMainFrame::OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult) {
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
    const ListRow& listRow = m_view[(size_t)row];
    const int sub = pItem->iSubItem;

    auto SetText = [&](const CString& s) {
        // 虚拟列表里 LVIF_TEXT 需要把字符串拷到系统缓冲区，不能直接给指针
        if (pItem->cchTextMax > 0 && s.GetLength() < pItem->cchTextMax) {
            _tcsncpy_s(pItem->pszText, pItem->cchTextMax, s, _TRUNCATE);
        } else {
            _tcsncpy_s(pItem->pszText, pItem->cchTextMax, s, _TRUNCATE);
        }
    };

    // 伪行（返回上层 / 文件夹）：只有名称列有内容
    if (listRow.entryIdx < 0) {
        if (sub == 0) {
            SetText(listRow.isParent ? CString(L"..（返回上层）")
                                     : CString(L"[") + listRow.name.c_str() + CString(L"]"));
        }
        *pResult = 0;
        return;
    }

    const size_t entryIdx = (size_t)listRow.entryIdx;
    const dpk::Entry& entry = m_reader.Entries()[entryIdx];
    EntryMeta& meta = m_meta[entryIdx];

    switch (sub) {
        case 0:  // 条目路径（文件夹收纳视图只显示文件名）
            if (m_folderMode && !listRow.name.empty()) {
                SetText(CString(listRow.name.c_str()));
            } else {
                SetText(W(entry.pathUtf8));
            }
            break;
        case 1: {  // 根块
            CString buf;
            buf.Format(L"%u", entry.rootBlock);
            SetText(buf);
            break;
        }
        case 2: {  // 块数
            CString buf;
            if (entry.blockCount > 0) {
                buf.Format(L"%u", entry.blockCount);
            } else {
                buf = L"-";
            }
            SetText(buf);
            break;
        }
        default: {
            // 明文、存储、标志、校验 —— 需要 whsc 头信息，懒做 PeekRootBlock
            if (!meta.headerOk && meta.err.empty()) {
                std::vector<uint8_t> stream;
                std::string err;
                if (m_reader.PeekRootBlock(entry.rootBlock, stream, err) &&
                    dpk::WhscPeekHeader(stream, meta.info, err)) {
                    meta.headerOk = true;
                } else {
                    meta.err = err;
                }
            }
            CString buf;
            if (meta.headerOk) {
                switch (sub) {
                    case 3: buf = BytesText(meta.info.plainSize); break;
                    case 4: buf = BytesText(meta.info.storedSize); break;
                    case 5: buf = FlagsText(meta.info); break;
                    case 6:
                        if (meta.verifyState == 1) buf = L"MD5 OK";
                        else if (meta.verifyState == 0) buf = L"失败";
                        else buf = L"未校验";
                        break;
                }
            } else {
                switch (sub) {
                    case 3: buf = L"—"; break;
                    case 4: buf = L"—"; break;
                    case 5: buf = L"头部损坏"; break;
                    case 6: buf = L"失败"; break;
                }
            }
            SetText(buf);
            break;
        }
    }
    *pResult = 0;
}

void CMainFrame::OnFilterChanged() {
    if (m_busy) return;
    if (m_reader.IsOpen() && m_reader.EntriesLoaded()) {
        RefreshList();
    }
}

void CMainFrame::OnFolderViewChanged() {
    if (m_busy) return;
    m_folderMode = m_chkFolder.GetCheck() == BST_CHECKED;
    if (!m_folderMode) m_curFolder.clear();
    if (m_reader.IsOpen() && m_reader.EntriesLoaded()) {
        RefreshList();
    }
}

void CMainFrame::OnListDblClk(NMHDR* pNMHDR, LRESULT* pResult) {
    const NMITEMACTIVATE* info = reinterpret_cast<const NMITEMACTIVATE*>(pNMHDR);
    *pResult = 0;
    const int row = info ? info->iItem : -1;
    if (row < 0 || row >= (int)m_view.size()) return;
    const ListRow& listRow = m_view[(size_t)row];
    if (m_folderMode && listRow.isParent) {
        // 返回上层：去掉最后一级
        const size_t slash = m_curFolder.rfind(L'/');
        m_curFolder = slash == std::wstring::npos ? std::wstring() : m_curFolder.substr(0, slash);
        RefreshList();
    } else if (m_folderMode && listRow.entryIdx < 0) {
        // 进入子文件夹
        m_curFolder = m_curFolder.empty() ? listRow.name : m_curFolder + L"/" + listRow.name;
        RefreshList();
    } else if (listRow.entryIdx >= 0) {
        // 双击文件：打开查看窗口
        ViewEntryAtRow(row);
    }
}

void CMainFrame::OnEntryChanged(NMHDR* pNMHDR, LRESULT* pResult) {
    LPNMLISTVIEW info = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
    if (!m_ignoreSel && (info->uChanged & LVIF_STATE) != 0 && (info->uNewState & LVIS_SELECTED) != 0) {
        ShowDetail(info->iItem);
    }
    *pResult = 0;
}

void CMainFrame::ShowDetail(int row) {
    if (row < 0 || row >= (int)m_view.size()) {
        m_detail.SetWindowText(L"");
        return;
    }
    const ListRow& listRow = m_view[(size_t)row];

    // 伪行：返回上层 / 文件夹概况
    if (listRow.entryIdx < 0) {
        if (listRow.isParent) {
            m_detail.SetWindowText(L".. 返回上层文件夹（双击或双击列表行进入）。");
            return;
        }
        const std::string prefix = dpk::WideToUtf8(m_curFolder.empty() ? listRow.name : m_curFolder + L"/" + listRow.name) + "/";
        size_t nFiles = 0;
        size_t nSub = 0;
        std::set<std::string> subNames;
        for (const dpk::Entry& e : m_reader.Entries()) {
            if (e.pathUtf8.compare(0, prefix.size(), prefix) != 0) continue;
            ++nFiles;
            const std::string rest = e.pathUtf8.substr(prefix.size());
            const size_t slash = rest.find('/');
            if (slash != std::string::npos) subNames.insert(rest.substr(0, slash));
        }
        nSub = subNames.size();
        CString text;
        text.Format(L"文件夹: %s\r\n路径: %s\r\n子文件夹 %zu 个，包内文件 %zu 条。\r\n双击进入。",
                    (LPCTSTR)listRow.name.c_str(),
                    (LPCTSTR)CString((m_curFolder.empty() ? listRow.name : m_curFolder + L"/" + listRow.name).c_str()),
                    nSub, nFiles);
        m_detail.SetWindowText(text);
        return;
    }

    const size_t idx = (size_t)listRow.entryIdx;
    const dpk::Entry& entry = m_reader.Entries()[idx];
    EntryMeta& meta = m_meta[idx];

    CString text;
    text += L"路径: " + W(entry.pathUtf8) + L"\r\n";
    text.AppendFormat(L"根块号: %u    块链长度: %u 块    包内偏移: %u 字节\r\n", entry.rootBlock, entry.blockCount,
                      entry.rootBlock * 1024);
    if (meta.headerOk) {
        text.AppendFormat(L"whsc 头: flags=0x%02X（%s）  明文 %s  存储 %s  内容口令=%s\r\n", meta.info.flags,
                          (LPCTSTR)FlagsText(meta.info), (LPCTSTR)BytesText(meta.info.plainSize),
                          (LPCTSTR)BytesText(meta.info.storedSize), meta.info.contentPassword ? L"是" : L"否");
    } else {
        text += L"whsc 头: 读取失败 - " + W(meta.err) + L"\r\n";
    }

    std::vector<uint8_t> plain;
    dpk::WhscInfo info;
    std::string err;
    const bool readOk = m_reader.ReadEntry(entry, plain, info, err);
    // 校验状态写回 meta，让虚拟列表下一帧重新渲染第 6 列
    if (readOk) {
        meta.verifyState = info.md5Ok ? 1 : 0;
        text.AppendFormat(L"校验: %s，解出明文 %s\r\n", info.md5Ok ? L"MD5 一致" : L"MD5 不匹配",
                          (LPCTSTR)BytesText(plain.size()));
    } else {
        meta.verifyState = 0;
        text += L"内容: 读取失败 - " + W(err) + L"\r\n";
    }
    m_ignoreSel = true;
    // 虚拟列表里 SetItem 不走，直接让该行所在区域重绘即可（控件会重新调 LVN_GETDISPINFO）
    CRect rc;
    m_list.GetItemRect(row, &rc, LVIR_BOUNDS);
    m_list.InvalidateRect(&rc);
    m_ignoreSel = false;

    m_detail.SetWindowText(text);
}

void CMainFrame::OnExtractAll() {
    if (m_busy) return;
    if (!m_reader.IsOpen()) {
        MessageBox(L"请先打开一个 DPK 包。", L"DPKViewer", MB_ICONINFORMATION);
        return;
    }
    std::wstring outDir;
    if (!app::PickFolder(m_hWnd, L"选择解包输出目录", outDir)) return;

    BusyGuard guard(m_busy);
    m_cancel = false;
    m_progress.SetPos(0);
    int okCount = 0;
    int failCount = 0;
    std::string err;
    std::vector<CString> failures;
    dpk::EntryResultCallback cb = [&](const std::string& pathUtf8, bool ok, const std::wstring& message) -> bool {
        if (!ok && failures.size() < 20) {
            failures.push_back(W(pathUtf8) + L" - " + CString(message.c_str()));
        }
        return true;
    };
    const bool completed = m_reader.ExtractAll(
        outDir, cb, [this](const std::wstring& m, int p) -> bool { return ProgressTick(m, p); }, okCount, failCount, err);
    m_progress.SetPos(100);

    CString summary;
    summary.Format(L"解包%s：成功 %d 条，失败 %d 条。\r\n输出目录: %s", (completed || m_cancel) ? L"结束" : L"失败",
                   okCount, failCount, (LPCTSTR)CString(outDir.c_str()));
    if (!err.empty()) summary += L"\r\n错误: " + W(err);
    for (size_t i = 0; i < failures.size(); ++i) {
        summary += L"\r\n  " + failures[i];
    }
    CString done;
    done.Format(L"解包完成：成功 %d 条，失败 %d 条 -> %s", okCount, failCount,
                (LPCTSTR)CString(outDir.c_str()));
    SetStatus(done);
    MessageBox(summary, L"DPKViewer - 解包", MB_ICONINFORMATION);
}

void CMainFrame::OnPackFolder() {
    if (m_busy) return;
    std::wstring srcDir;
    if (!app::PickFolder(m_hWnd, L"选择要打包的源文件夹", srcDir)) return;

    CFileDialog dlg(FALSE, L"dpk", L"package.dpk", OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
                    L"DPK 包 (*.dpk)|*.dpk|所有文件 (*.*)|*.*||", this);
    dlg.m_ofn.lpstrTitle = L"选择生成的 DPK 包路径";
    if (dlg.DoModal() != IDOK) return;
    const std::wstring outPkg = std::wstring(dlg.GetPathName().GetString());

    dpk::PackOptions opt;
    opt.compress = m_chkCompress.GetCheck() == BST_CHECKED;
    opt.encrypt = m_chkEncrypt.GetCheck() == BST_CHECKED;
    opt.level = 9;
    if (m_chkTemplate.GetCheck() == BST_CHECKED) {
        if (!m_reader.IsOpen()) {
            MessageBox(L"勾选「沿用已打开包的包头」前，请先打开一个包作为模板。", L"DPKViewer", MB_ICONINFORMATION);
            return;
        }
        std::string terr;
        if (!dpk::LoadTemplateHeader(m_reader.Path(), opt.templateHeader, terr)) {
            MessageBox(W("读取模板包头失败: " + terr), L"DPKViewer", MB_ICONWARNING);
            return;
        }
        // 同时沿用原包块布局：按原包块号回填，保证总块数与结构一致
        auto layout = std::make_shared<dpk::PackLayout>();
        if (!dpk::LoadPackLayout(m_reader.Path(), *layout, terr,
                                 [this](const std::wstring& m, int p) -> bool { return ProgressTick(m, p / 7); })) {
            MessageBox(W("读取模板布局失败: " + terr), L"DPKViewer", MB_ICONWARNING);
            return;
        }
        opt.layout = layout;
    }

    BusyGuard guard(m_busy);
    m_cancel = false;
    m_progress.SetPos(0);
    dpk::PackResult result;
    std::string err;
    const bool ok =
        dpk::PackFolder(srcDir, outPkg, opt, result, err,
                        [this](const std::wstring& m, int p) -> bool { return ProgressTick(m, p); });
    m_progress.SetPos(100);

    CString summary;
    if (ok) {
        summary.Format(L"生成%s：写入 %u 条（其中复用原包 %u 条），跳过 %u 条。\r\n明文总量 %s，包大小 %s，总块数 %u。\r\n"
                       L"压缩=%s，加密=%s，模板包头=%s\r\n输出: %s",
                       m_cancel ? L"已取消" : L"完成", result.entryCount, result.reusedCount, result.skippedCount,
                       (LPCTSTR)BytesText(result.plainBytes), (LPCTSTR)BytesText(result.packageSize),
                       result.totalBlocks, opt.compress ? L"是" : L"否", opt.encrypt ? L"是" : L"否",
                       opt.templateHeader.empty() ? L"无" : L"有", (LPCTSTR)CString(outPkg.c_str()));
        CString done;
        done.Format(L"生成完成：%u 条 -> %s", result.entryCount, (LPCTSTR)CString(outPkg.c_str()));
        SetStatus(done);
    } else {
        summary.Format(L"生成失败：%s", (LPCTSTR)W(err));
        SetStatus(summary);
    }
    MessageBox(summary, L"DPKViewer - 生成", ok ? MB_ICONINFORMATION : MB_ICONWARNING);
}

void CMainFrame::OnComparePackages() {
    if (m_compareWnd != nullptr && ::IsWindow(m_compareWnd->GetSafeHwnd())) {
        if (m_compareWnd->IsIconic()) {
            m_compareWnd->ShowWindow(SW_RESTORE);
        }
        m_compareWnd->SetForegroundWindow();
        return;
    }
    m_compareWnd = new CCompareWindow();
    if (!m_compareWnd->Create(this)) {
        delete m_compareWnd;
        m_compareWnd = nullptr;
        MessageBox(L"创建对比窗口失败。", L"DPKViewer", MB_ICONWARNING);
        return;
    }
    m_compareWnd->ShowWindow(SW_SHOW);
    m_compareWnd->UpdateWindow();
}

LRESULT CMainFrame::OnCompareClosed(WPARAM wParam, LPARAM lParam) {
    // 对比窗口自销毁后回传，避免悬空指针
    m_compareWnd = nullptr;
    return 0;
}

void CMainFrame::OnUsage() {
    CString text;
    text += L"DPKViewer - 寻仙 DPK 包（whpackage1.0）浏览 / 解包 / 生成工具\r\n";
    text += L"格式与读写语义以参考实现 dpk.py 为准，详见 docs/DPK_FORMAT.md。\r\n\r\n";
    text += L"1. 浏览：点「打开包…」选择 .dpk；列表显示条目路径、根块号、块链长度、明文/存储大小、"
            L"压缩与加密标志；选中条目后在下方查看头部字段、MD5 校验结果与内容预览；"
            L"顶部「筛选」按路径子串即时过滤。\r\n";
    text += L"   「文件夹收纳」（默认开）以文件夹方式浏览：列表按子文件夹 + 文件分组，"
            L"双击文件夹进入、双击首行「..（返回上层）」返回；取消勾选则恢复全路径平铺列表；"
            L"双击文件或右键「查看」打开查看窗口（文本直接显示，乱码或二进制自动/手动切换十六进制）；"
            L"右键还可「导入文件到此处」（导入到选中文件所在目录，同名替换、未变条目字节级复用原包）、"
            L"「导出文件」/「导出文件夹」。\r\n";
    text += L"2. 解包：点「解包到目录…」选择输出目录，逐条导出并校验 MD5；"
            L"单条失败不影响其余条目；进度中按 Esc 可取消。\r\n";
    text += L"3. 生成：点「生成 DPK 包…」选择源文件夹与输出路径，按勾选项决定压缩（zlib level 9）与加密"
            L"（包口令 tianxiawudi）；勾选「沿用已打开包的包头」可用当前包的 1024 字节包头为模板，"
            L"仅改写其中的目录树/路径表块号。\r\n";
    text += L"4. 对比：菜单「工具 → 对比 DPK 包…」打开对比窗口，选择包 A / 包 B 后开始对比；"
            L"三档深度：仅路径（只比条目清单）、标准（路径 + 流头字段 + 内嵌 MD5 摘要，每条目只读根块 1KB）、"
            L"完整（逐条目解码并逐字节比对，可检出包内损坏）；结果可按状态筛选（含「仅内容不同」），"
            L"双击「不同」条目可查看内容对比（文本走行级 diff：部分修改的行自动配对并高亮行内差异片段，"
            L"非文本走十六进制逐字节比对），"
            L"也可导出文本报告。「大小差异上限」可防止误选文件：两包大小差异超过设定 MB 数时提示并停止对比"
            L"（默认 100，可改任意数值或「不限制」）。\r\n\r\n";
    text += L"命令行（同一 exe，不弹界面）：\r\n";
    text += L"  DPKViewer.exe /list    <包>\r\n";
    text += L"  DPKViewer.exe /extract <包> <目录>\r\n";
    text += L"  DPKViewer.exe /pack    <源目录> <输出包> [/level N] [/nocompress] [/noencrypt] [/template <包>]\r\n";
    text += L"  DPKViewer.exe /compare <包A> <包B> [/paths|/meta|/full]\r\n";
    text += L"  附加：/log <文件>  /quiet\r\n";
    MessageBox(text, L"DPKViewer - 使用说明", MB_ICONINFORMATION);
}

void CMainFrame::OnAbout() {
    CString text;
    text += L"DPKViewer v0.1.4\r\n";
    text += L"MFC（静态链接）实现的 DPK 包工具：浏览、解包、生成。\r\n\r\n";
    text += L"包格式：whpackage1.0（块大小 1024，包口令 tianxiawudi，内容口令 gandiaosifu）\r\n";
    text += L"流格式：whsc1.0（flags 位 0x01 压缩 / 0x02 加密 / 0x04 内容口令）\r\n";
    text += L"加密：DES 变体（CBC 零 IV + 尾块混淆），密钥取口令前 8 字节\r\n";
    text += L"压缩：zlib（deflate level 9）\r\n\r\n";
    text += L"工程含核心模块自检与参考实现双向交叉验证（tests/）。";
    MessageBox(text, L"关于 DPKViewer", MB_ICONINFORMATION);
}

void CMainFrame::OnAppExit() {
    PostMessage(WM_CLOSE);
}