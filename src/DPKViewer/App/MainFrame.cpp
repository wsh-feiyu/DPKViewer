// MainFrame.cpp - DPKViewer 主窗口实现：浏览 / 解包 / 生成 三条功能链路
#include <afxwin.h>
#include <afxcmn.h>
#include <afxdlgs.h>

#include "MainFrame.h"
#include "AppUtil.h"
#include "resource.h"

#include "Core/DpkReader.h"
#include "Core/DpkWriter.h"
#include "Core/FileIO.h"
#include "Core/StrUtil.h"
#include "Core/Whsc.h"

#pragma comment(lib, "comctl32.lib")

namespace {

const int kMargin = 8;
const int kRowH = 26;
const int kRowGap = 6;
const int kDetailH = 180;
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

CString PreviewText(const std::vector<uint8_t>& plain, size_t maxBytes) {
    if (plain.empty()) return L"（空文件）";
    const size_t n = plain.size() < maxBytes ? plain.size() : maxBytes;
    if (dpk::LooksLikeText(&plain[0], n)) {
        std::string head(reinterpret_cast<const char*>(&plain[0]), n);
        CString text = W(dpk::NameBytesToWide(head));
        if (plain.size() > n) text += L"\r\n…（已截断）";
        return text;
    }
    CString text = W(dpk::HexDump(&plain[0], plain.size(), maxBytes));
    if (plain.size() > maxBytes) text += L"\r\n…（已截断）";
    return text;
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
    ON_COMMAND(ID_APP_USAGE, &CMainFrame::OnUsage)
    ON_COMMAND(ID_APP_ABOUT, &CMainFrame::OnAbout)
    ON_COMMAND(ID_APP_EXIT, &CMainFrame::OnAppExit)
    ON_BN_CLICKED(IDC_BTN_OPEN, &CMainFrame::OnOpenPackage)
    ON_BN_CLICKED(IDC_BTN_EXTRACT, &CMainFrame::OnExtractAll)
    ON_BN_CLICKED(IDC_BTN_PACK, &CMainFrame::OnPackFolder)
    ON_BN_CLICKED(IDC_BTN_USAGE, &CMainFrame::OnUsage)
    ON_BN_CLICKED(IDC_BTN_ABOUT, &CMainFrame::OnAbout)
    ON_EN_CHANGE(IDC_EDIT_FILTER, &CMainFrame::OnFilterChanged)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_ENTRY_LIST, &CMainFrame::OnEntryChanged)
    ON_NOTIFY(LVN_GETDISPINFO, IDC_ENTRY_LIST, &CMainFrame::OnGetdispinfo)
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
    m_chkCompress.SetCheck(BST_CHECKED);
    m_chkEncrypt.SetCheck(BST_CHECKED);

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

    // 只构建过滤后的下标映射，不做任何 I/O
    // LVS_OWNERDATA 虚拟列表：SetItemCountEx 后由 LVN_GETDISPINFO 按需填充
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!filterU8.empty()) {
            const std::string lower = dpk::ToLowerAscii(entries[i].pathUtf8);
            if (lower.find(filterU8) == std::string::npos) continue;
        }
        m_view.push_back((int)i);
    }
    m_list.SetItemCountEx((int)m_view.size(), LVSICF_NOSCROLL);

    m_ignoreSel = false;
    CString status;
    status.Format(L"条目 %u 条，当前显示 %zu 条。", (unsigned)entries.size(), m_view.size());
    SetStatus(status);
}

// 虚拟列表回调：系统需要某行某列显示什么文字时才会进来
// 每行进来一次，PeekRootBlock 懒做 + 结果缓存到 m_meta，滚动回来不重读
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
    const size_t entryIdx = (size_t)m_view[row];
    const dpk::Entry& entry = m_reader.Entries()[entryIdx];
    EntryMeta& meta = m_meta[entryIdx];
    const int sub = pItem->iSubItem;

    auto SetText = [&](const CString& s) {
        // 虚拟列表里 LVIF_TEXT 需要把字符串拷到系统缓冲区，不能直接给指针
        if (pItem->cchTextMax > 0 && s.GetLength() < pItem->cchTextMax) {
            _tcsncpy_s(pItem->pszText, pItem->cchTextMax, s, _TRUNCATE);
        } else {
            _tcsncpy_s(pItem->pszText, pItem->cchTextMax, s, _TRUNCATE);
        }
    };

    switch (sub) {
        case 0:  // 条目路径
            SetText(W(entry.pathUtf8));
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
    const size_t idx = (size_t)m_view[row];
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

    text += L"---- 内容预览 ----\r\n";
    if (readOk) {
        text += PreviewText(plain, 2048);
    } else {
        text += L"内容读取失败，无法预览。";
    }
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
        if (!dpk::LoadPackLayout(m_reader.Path(), *layout, terr)) {
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
        summary.Format(L"生成%s：写入 %u 条，跳过 %u 条。\r\n明文总量 %s，包大小 %s，总块数 %u。\r\n"
                       L"压缩=%s，加密=%s，模板包头=%s\r\n输出: %s",
                       m_cancel ? L"已取消" : L"完成", result.entryCount, result.skippedCount,
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

void CMainFrame::OnUsage() {
    CString text;
    text += L"DPKViewer - 寻仙 DPK 包（whpackage1.0）浏览 / 解包 / 生成工具\r\n";
    text += L"格式与读写语义以参考实现 dpk.py 为准，详见 docs/DPK_FORMAT.md。\r\n\r\n";
    text += L"1. 浏览：点「打开包…」选择 .dpk；列表显示条目路径、根块号、块链长度、明文/存储大小、"
            L"压缩与加密标志；选中条目后在下方查看头部字段、MD5 校验结果与内容预览；"
            L"顶部「筛选」按路径子串即时过滤。\r\n";
    text += L"2. 解包：点「解包到目录…」选择输出目录，逐条导出并校验 MD5；"
            L"单条失败不影响其余条目；进度中按 Esc 可取消。\r\n";
    text += L"3. 生成：点「生成 DPK 包…」选择源文件夹与输出路径，按勾选项决定压缩（zlib level 9）与加密"
            L"（包口令 tianxiawudi）；勾选「沿用已打开包的包头」可用当前包的 1024 字节包头为模板，"
            L"仅改写其中的目录树/路径表块号。\r\n\r\n";
    text += L"命令行（同一 exe，不弹界面）：\r\n";
    text += L"  DPKViewer.exe /list    <包>\r\n";
    text += L"  DPKViewer.exe /extract <包> <目录>\r\n";
    text += L"  DPKViewer.exe /pack    <源目录> <输出包> [/level N] [/nocompress] [/noencrypt] [/template <包>]\r\n";
    text += L"  附加：/log <文件>  /quiet\r\n";
    MessageBox(text, L"DPKViewer - 使用说明", MB_ICONINFORMATION);
}

void CMainFrame::OnAbout() {
    CString text;
    text += L"DPKViewer 1.0.0\r\n";
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