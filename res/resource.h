// resource.h - DPKViewer resource IDs
// NOTE 1: keep this file ASCII-only. it is included by both the C++ sources and by
//         rc.exe (DPKViewer.rc), and the resource compiler reads it before the
//         UTF-8 code page takes effect.
// NOTE 2: the file must end with a newline. rc.exe drops the last line of an
//         included file when it is not newline-terminated, which loses the
//         closing #endif and fails with RC1004 (unexpected end of file).
#pragma once

#define IDR_MAINFRAME 128  // application icon + main menu (same id, different types)

// main window controls
#define IDC_BTN_OPEN 1001
#define IDC_BTN_EXTRACT 1002
#define IDC_BTN_PACK 1003
#define IDC_BTN_USAGE 1004
#define IDC_BTN_ABOUT 1005
#define IDC_LBL_FILTER 1006
#define IDC_EDIT_FILTER 1007
#define IDC_CHK_COMPRESS 1008
#define IDC_CHK_ENCRYPT 1009
#define IDC_CHK_TEMPLATE 1010
#define IDC_ENTRY_LIST 1011
#define IDC_DETAIL 1012
#define IDC_PROGRESS 1013
#define IDC_STATUS 1014

// menu commands
#define ID_APP_OPEN 32771
#define ID_APP_EXTRACT 32772
#define ID_APP_PACK 32773
#define ID_APP_USAGE 32774
// ID_APP_ABOUT / ID_APP_EXIT are provided by MFC's afxres.h (0xE140 / 0xE141);
// the definitions below are only a fallback when afxres.h is not included.
#ifndef ID_APP_ABOUT
#define ID_APP_ABOUT 0xE140
#endif
#ifndef ID_APP_EXIT
#define ID_APP_EXIT 0xE141
#endif
