// TextDiffProbe.cpp - TextDiff 行级对齐自检（独立编译：cl /utf-8 TextDiffProbe.cpp ..\src\DPKViewer\Core\TextDiff.cpp）
#include <cstdio>
#include <string>
#include <vector>

#include "../src/DPKViewer/Core/TextDiff.h"

static const wchar_t* KindName(uint8_t kind) {
    switch (kind) {
        case 0: return L"SAME";
        case 1: return L"CHNG";
        case 2: return L"A-ONLY";
        case 3: return L"B-ONLY";
    }
    return L"?";
}

static void Dump(const char* tag, const std::vector<std::wstring>& la, const std::vector<std::wstring>& lb,
                 const std::vector<dpk::TextDiffRow>& rows) {
    printf("[%s] rows=%zu\n", tag, rows.size());
    for (const dpk::TextDiffRow& r : rows) {
        wprintf(L"  %-6s A[%d]=%-42s B[%d]=%s\n", KindName(r.kind), r.aIdx,
                r.aIdx >= 0 ? la[(size_t)r.aIdx].c_str() : L"-", r.bIdx, r.bIdx >= 0 ? lb[(size_t)r.bIdx].c_str() : L"-");
    }
}

int wmain() {
    // 场景1：表格行部分数字变化 + 中间插入新行（对应截图场景的缩影）
    std::vector<std::wstring> la = {L"1*1*1 1000000 100 100 1000000 100 100 6000000", L"111111 1000000 90"};
    std::vector<std::wstring> lb;
    for (int i = 1; i <= 9; ++i) {
        wchar_t buf[64];
        swprintf(buf, 64, L"%d*%d*%d 1 90 100 1 90 100 1 90", i, i, i);
        lb.emplace_back(buf);
    }
    lb.push_back(L"111111 1000000 90");  // 与 A 侧第 2 行完全相同
    std::vector<dpk::TextDiffRow> rows;
    dpk::TextDiffRows(la, lb, rows);
    Dump("case1 partial-change", la, lb, rows);

    // 场景2：纯插入不应误配
    std::vector<std::wstring> la2 = {L"alpha"};
    std::vector<std::wstring> lb2 = {L"alpha", L"beta"};
    rows.clear();
    dpk::TextDiffRows(la2, lb2, rows);
    Dump("case2 pure-insert", la2, lb2, rows);

    // 场景3：同类行小改动应配成修改
    std::vector<std::wstring> la3 = {L"hello world 123"};
    std::vector<std::wstring> lb3 = {L"hello world 456"};
    rows.clear();
    dpk::TextDiffRows(la3, lb3, rows);
    Dump("case3 small-edit", la3, lb3, rows);

    // 场景4：完全无关的行不应配对
    std::vector<std::wstring> la4 = {L"aaaaaaaaaa"};
    std::vector<std::wstring> lb4 = {L"zzzzzzzzzz"};
    rows.clear();
    dpk::TextDiffRows(la4, lb4, rows);
    Dump("case4 unrelated", la4, lb4, rows);

    // 场景5：数值表多行同结构不同数值 —— 全部应配成 CHNG（截图问题的主场景）
    std::vector<std::wstring> la5, lb5;
    for (int i = 1; i <= 5; ++i) {
        wchar_t a[96], b[96];
        swprintf(a, 96, L"item_%03d name%d 100%d 200 %d000 end", i, i, i * 10, i);
        swprintf(b, 96, L"item_%03d name%d 100%d 200 %d000 end", i, i + 1, i * 10 + 7, i + 3);
        la5.emplace_back(a);
        lb5.emplace_back(b);
    }
    rows.clear();
    dpk::TextDiffRows(la5, lb5, rows);
    Dump("case5 numeric-table", la5, lb5, rows);

    // 场景6：同结构行 + 中间夹一行真正新增的行
    std::vector<std::wstring> la6 = {L"row 100 alpha", L"row 200 beta"};
    std::vector<std::wstring> lb6 = {L"row 150 alpha", L"INSERTED brand new line", L"row 250 beta"};
    rows.clear();
    dpk::TextDiffRows(la6, lb6, rows);
    Dump("case6 table-with-insert", la6, lb6, rows);

    // 场景7：结构不同的行不应被归一化误配
    std::vector<std::wstring> la7 = {L"111111 1000000 90"};
    std::vector<std::wstring> lb7 = {L"2*2*2 1 90 100 1 90 100 1 90"};
    rows.clear();
    dpk::TextDiffRows(la7, lb7, rows);
    Dump("case7 different-structure", la7, lb7, rows);

    // 场景8：equip_intensify_level 真实数据缩影——
    // A 侧列数不一（14/8/2 列），B 侧统一 17 列，行身份在行号+代码两列
    {
        std::vector<std::wstring> a8, b8;
        a8.emplace_back(L"");
        b8.emplace_back(L"");
        const wchar_t* codes[] = {L"1*1*1", L"1*1*1", L"1*1*1", L"2*2*2", L"3*3*3", L"15*5*17"};
        for (int i = 0; i < 6; ++i) {
            wchar_t a[128], b[128];
            if (i < 3) {
                swprintf(a, 128, L"%d\t%s\t%d000\t%d\t70\t%d000\t%d\t70\t\t\t", i + 1, codes[i], 100 + i, 50 + i,
                         100 + i, 50 + i);
            } else if (i < 5) {
                swprintf(a, 128, L"%d\t%s\t%d000\t%d\t70", i + 1, codes[i], 200 + i, 10 + i);
            } else {
                swprintf(a, 128, L"%d\t%s", i + 1, codes[i]);
            }
            swprintf(b, 128,
                     L"%d\t%s\t1\t90\t100\t1\t90\t100\t1\t90\t100\t1\t90\t100\t1\t90\t100", i + 1, codes[i]);
            a8.emplace_back(a);
            b8.emplace_back(b);
        }
        rows.clear();
        dpk::TextDiffRows(a8, b8, rows);
        Dump("case8 intensify-table(real)", a8, b8, rows);
    }

    // 场景9：相似度数值抽查
    printf("sim(1*1*1...) = %.3f (expect >= 0.30)\n",
           dpk::TextLineSimilarity(la[0], lb[0]));
    printf("sim(unrelated) = %.3f (expect < 0.30)\n",
           dpk::TextLineSimilarity(L"aaaaaaaaaa", L"zzzzzzzzzz"));
    return 0;
}
