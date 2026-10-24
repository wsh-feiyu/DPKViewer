// TextDiff.h - 行级文本 diff（纯逻辑，不依赖 MFC，供 DiffViewer 与测试使用）
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dpk {

// 一行对齐结果：aIdx/bIdx 为该行在两侧行数组中的下标（-1 表示该侧无对应行）
struct TextDiffRow {
    int aIdx = -1;
    int bIdx = -1;
    uint8_t kind = 0;  // 0 相同 / 1 修改（相似行配对）/ 2 A 独有 / 3 B 独有
};

// 行级对齐：先做精确 LCS，再把相邻的「A 独有块 + B 独有块」按行相似度二次对齐，
// 只有部分内容变化的行会配成 kind=1（修改），而不是拆成两侧独有。
// 返回 false 表示行数超出动态规划容量，退化为仅对齐公共前后缀。
bool TextDiffRows(const std::vector<std::wstring>& la, const std::vector<std::wstring>& lb,
                  std::vector<TextDiffRow>& rows);

// 行相似度（0..1）：字符级公共前后缀与令牌级 LCS 的较大值
double TextLineSimilarity(const std::wstring& a, const std::wstring& b, bool allowToken = true);

// 行内差异片段边界（供渲染高亮「行内哪一段变了」）
size_t TextCommonPrefix(const std::wstring& a, const std::wstring& b);
size_t TextCommonSuffix(const std::wstring& a, const std::wstring& b, size_t cap);

}  // namespace dpk
