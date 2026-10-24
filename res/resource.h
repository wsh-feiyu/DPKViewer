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

// compare window controls

// compare window controls
#define IDC_CMP_EDIT_A 1016
#define IDC_CMP_BTN_A 1017
#define IDC_CMP_EDIT_B 1018
#define IDC_CMP_BTN_B 1019
#define IDC_CMP_DEPTH 1020
#define IDC_CMP_RUN 1021
#define IDC_CMP_EXPORT 1022
#define IDC_CMP_CLOSE 1023
#define IDC_CMP_FILTER 1024
#define IDC_CMP_LIST 1025
#define IDC_CMP_DETAIL 1026
#define IDC_CMP_PROGRESS 1027
#define IDC_CMP_STATUS 1028
#define IDC_CMP_STATS 1029
#define IDC_CMP_PKGINFO 1030

// content diff viewer controls
#define IDC_DIFF_PATH 1032
#define IDC_DIFF_MODE 1033
#define IDC_DIFF_CLOSE 1034
#define IDC_DIFF_TEXT_A 1035
#define IDC_DIFF_TEXT_B 1036
#define IDC_DIFF_STATUS 1037
#define IDC_CHK_FOLDER 1038
#define IDC_CMP_SIZELIMIT 1031

// menu commands
#define ID_APP_OPEN 32771
#define ID_APP_EXTRACT 32772
#define ID_APP_PACK 32773
#define ID_APP_USAGE 32774
#define ID_APP_COMPARE 32776
#define ID_CTX_EXPORT_FILE 32777   // 右键菜单：导出单个文件条目
#define ID_CTX_EXPORT_FOLDER 32778 // 右键菜单：导出文件夹（递归）
// ID_APP_ABOUT / ID_APP_EXIT are provided by MFC's afxres.h (0xE140 / 0xE141);
// the definitions below are only a fallback when afxres.h is not included.
#ifndef ID_APP_ABOUT
#define ID_APP_ABOUT 0xE140
#endif
#ifndef ID_APP_EXIT
#define ID_APP_EXIT 0xE141
#endif
