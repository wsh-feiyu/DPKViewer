// TextDiff.cpp - 行级文本 diff 实现：精确 LCS → 归一化配对 → 相似度配对
#include "TextDiff.h"

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <map>

namespace dpk {

namespace {

// 第三层（相似度）阈值：达到才配成「修改」；过低会把无关行误配
const double kSimThreshold = 0.30;
// 整体 LCS 动态规划单元上限（超出退化为公共前后缀对齐）
const double kMaxLcsCells = 12000000.0;
// 替换块内对齐单元上限（过大只做位置贪心，保证界面不卡）
const double kMaxAlignCells = 1000000.0;
// 令牌级相似度参与对齐的单元上限（令牌 LCS 开销较大）
const double kMaxTokenCells = 100000.0;

struct DiffOp {
    int kind;  // 0 相等 1 A独有 2 B独有
    int aIdx;
    int bIdx;
};

size_t CommonPrefixLen(const std::wstring& a, const std::wstring& b) {
    const size_t lim = a.size() < b.size() ? a.size() : b.size();
    size_t p = 0;
    while (p < lim && a[p] == b[p]) ++p;
    return p;
}

size_t CommonSuffixLen(const std::wstring& a, const std::wstring& b, size_t cap) {
    size_t s = 0;
    while (s < cap && a[a.size() - 1 - s] == b[b.size() - 1 - s]) ++s;
    return s;
}

void TokenizeLine(const std::wstring& line, std::vector<std::wstring>& toks) {
    toks.clear();
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && iswspace(line[i])) ++i;
        const size_t start = i;
        while (i < line.size() && !iswspace(line[i])) ++i;
        if (i > start) toks.emplace_back(line, start, i - start);
    }
}

// 令牌级 LCS 长度（调用方保证各侧不超过 64 个令牌）
int TokenLcsLen(const std::vector<std::wstring>& ta, const std::vector<std::wstring>& tb) {
    static uint16_t buf[65 * 65];
    const size_t w = tb.size() + 1;
    memset(buf, 0, sizeof(uint16_t) * (ta.size() + 1) * w);
    for (size_t i = 1; i <= ta.size(); ++i) {
        for (size_t j = 1; j <= tb.size(); ++j) {
            if (ta[i - 1] == tb[j - 1]) {
                buf[i * w + j] = (uint16_t)(buf[(i - 1) * w + j - 1] + 1);
            } else {
                buf[i * w + j] = buf[(i - 1) * w + j] >= buf[i * w + j - 1] ? buf[(i - 1) * w + j]
                                                                            : buf[i * w + j - 1];
            }
        }
    }
    return buf[ta.size() * w + tb.size()];
}

// 行首键：归一化行的前 kKeyTokens 个 token——
// 表格数据的行身份几乎总在开头的几列（行号 / 片段代码等），
// 后面列数增减、数值全变时仍能按行键保序配对
const size_t kKeyTokens = 2;

std::wstring PrefixKeyOf(const std::wstring& normalized) {
    size_t tokens = 0;
    size_t end = 0;
    for (size_t i = 0; i < normalized.size() && tokens < kKeyTokens; ++i) {
        if (normalized[i] == L' ') {
            ++tokens;
            end = (tokens < kKeyTokens) ? i + 1 : i;
        }
    }
    if (tokens < kKeyTokens) end = normalized.size();
    return normalized.substr(0, end);
}

// 归一化：数字折叠为 '#'、空白折叠为单空格并去首尾——
// 「结构相同但数值不同」的表格行归一化后相同，可稳定配对
std::wstring NormalizeLine(const std::wstring& s) {
    std::wstring r;
    r.reserve(s.size() + 4);
    wchar_t last = 0;
    for (const wchar_t c : s) {
        wchar_t t;
        if (iswdigit(c)) {
            t = L'#';
        } else if (iswspace(c)) {
            t = L' ';
        } else {
            t = c;
        }
        if (t == L'#' && last == L'#') continue;
        if (t == L' ' && (last == L' ' || r.empty())) continue;
        r.push_back(t);
        last = t;
    }
    while (!r.empty() && r[r.size() - 1] == L' ') r.erase(r.size() - 1);
    return r;
}

}  // namespace

double TextLineSimilarity(const std::wstring& a, const std::wstring& b, bool allowToken) {
    if (&a == &b || a == b) return 1.0;
    const size_t p = CommonPrefixLen(a, b);
    const size_t lim = (a.size() < b.size() ? a.size() : b.size()) - p;
    const size_t s = CommonSuffixLen(a, b, lim);
    const size_t total = a.size() + b.size();
    const double cs = total == 0 ? 1.0 : 2.0 * (double)(p + s) / (double)total;
    if (!allowToken || cs >= kSimThreshold || a.size() < 2 || b.size() < 2) return cs;
    // 字符级相似度不够时改用令牌级：表格行里数字变化大但结构相同的情况
    std::vector<std::wstring> ta, tb;
    TokenizeLine(a, ta);
    TokenizeLine(b, tb);
    if (ta.empty() || tb.empty() || ta.size() > 64 || tb.size() > 64) return cs;
    const int l = TokenLcsLen(ta, tb);
    const double ts = 2.0 * (double)l / (double)(ta.size() + tb.size());
    return ts > cs ? ts : cs;
}

size_t TextCommonPrefix(const std::wstring& a, const std::wstring& b) {
    return CommonPrefixLen(a, b);
}

size_t TextCommonSuffix(const std::wstring& a, const std::wstring& b, size_t cap) {
    return CommonSuffixLen(a, b, cap);
}

bool TextDiffRows(const std::vector<std::wstring>& la, const std::vector<std::wstring>& lb,
                  std::vector<TextDiffRow>& rows) {
    rows.clear();
    const size_t n = la.size();
    const size_t m = lb.size();

    if ((double)n * (double)m > kMaxLcsCells) {
        // 退化为公共前缀/后缀对齐，中段整体标记
        size_t p = 0;
        while (p < n && p < m && la[p] == lb[p]) ++p;
        size_t sa = n, sb = m;
        while (sa > p && sb > p && la[sa - 1] == lb[sb - 1]) {
            --sa;
            --sb;
        }
        for (size_t k = 0; k < p; ++k) rows.push_back(TextDiffRow{(int)k, (int)k, 0});
        for (size_t k = p; k < sa; ++k) rows.push_back(TextDiffRow{(int)k, -1, 2});
        for (size_t k = p; k < sb; ++k) rows.push_back(TextDiffRow{-1, (int)k, 3});
        const size_t tail = std::min(n - sa, m - sb);
        for (size_t t = 0; t < tail; ++t) rows.push_back(TextDiffRow{(int)(sa + t), (int)(sb + t), 0});
        return false;
    }

    // ---- 第一层：整行精确 LCS ----
    std::map<std::wstring, int> pool;
    auto intern = [&pool](const std::vector<std::wstring>& ls, std::vector<int>& ids, std::vector<int>* sink) {
        ids.reserve(ls.size());
        for (const std::wstring& s : ls) {
            auto it = pool.find(s);
            if (it == pool.end()) it = pool.emplace(s, (int)pool.size()).first;
            ids.push_back(it->second);
            if (sink) sink->push_back(it->second);
        }
    };
    std::vector<int> va, vb;
    intern(la, va, nullptr);
    intern(lb, vb, nullptr);

    const size_t w = m + 1;
    std::vector<uint16_t> lcs((n + 1) * w, 0);
    std::vector<uint8_t> dir((n + 1) * w, 0);
    for (size_t i = 1; i <= n; ++i) {
        for (size_t j = 1; j <= m; ++j) {
            const size_t cur = i * w + j;
            if (va[i - 1] == vb[j - 1]) {
                lcs[cur] = (uint16_t)(lcs[(i - 1) * w + j - 1] + 1);
                dir[cur] = 0;
            } else if (lcs[(i - 1) * w + j] >= lcs[cur - 1]) {
                lcs[cur] = lcs[(i - 1) * w + j];
                dir[cur] = 1;
            } else {
                lcs[cur] = lcs[cur - 1];
                dir[cur] = 2;
            }
        }
    }
    std::vector<DiffOp> ops;
    {
        size_t i = n, j = m;
        while (i > 0 || j > 0) {
            if (i > 0 && j > 0 && dir[i * w + j] == 0) {
                ops.push_back(DiffOp{0, (int)(i - 1), (int)(j - 1)});
                --i;
                --j;
            } else if (j > 0 && (i == 0 || dir[i * w + j] == 2)) {
                ops.push_back(DiffOp{2, -1, (int)(j - 1)});
                --j;
            } else {
                ops.push_back(DiffOp{1, (int)(i - 1), -1});
                --i;
            }
        }
        std::reverse(ops.begin(), ops.end());
    }

    // 预归一化全部行（第二层配对用）并预算行首键（第 2.5 层配对用）
    std::vector<std::wstring> nlA(n), nlB(m), keyA(n), keyB(m);
    for (size_t k = 0; k < n; ++k) {
        nlA[k] = NormalizeLine(la[k]);
        keyA[k] = PrefixKeyOf(nlA[k]);
    }
    for (size_t k = 0; k < m; ++k) {
        nlB[k] = NormalizeLine(lb[k]);
        keyB[k] = PrefixKeyOf(nlB[k]);
    }

    auto sameRow = [&](int ai, int bi) { rows.push_back(TextDiffRow{ai, bi, 0}); };
    auto delRow = [&](int ai) { rows.push_back(TextDiffRow{ai, -1, 2}); };
    auto insRow = [&](int bi) { rows.push_back(TextDiffRow{-1, bi, 3}); };
    auto pairRow = [&](int ai, int bi) { rows.push_back(TextDiffRow{ai, bi, 1}); };

    // ---- 第三层：相似度加权对齐（对剩余未配对的连续小段兜底） ----
    // 匹配得分 = 行相似度，取总相似度最大的保序配对序列
    auto simAlign = [&](const std::vector<int>& xs, const std::vector<int>& ys) {
        const size_t bn = xs.size();
        const size_t bm = ys.size();
        if (bn == 0) {
            for (size_t k = 0; k < bm; ++k) insRow(ys[k]);
            return;
        }
        if (bm == 0) {
            for (size_t k = 0; k < bn; ++k) delRow(xs[k]);
            return;
        }
        if ((double)bn * (double)bm > kMaxAlignCells) {
            const size_t cnt = bn < bm ? bn : bm;
            for (size_t k = 0; k < cnt; ++k) {
                if (TextLineSimilarity(la[(size_t)xs[k]], lb[(size_t)ys[k]], false) >= kSimThreshold) {
                    pairRow(xs[k], ys[k]);
                } else {
                    delRow(xs[k]);
                    insRow(ys[k]);
                }
            }
            for (size_t k = cnt; k < bn; ++k) delRow(xs[k]);
            for (size_t k = cnt; k < bm; ++k) insRow(ys[k]);
            return;
        }
        const bool allowToken = (double)bn * (double)bm <= kMaxTokenCells;
        std::vector<float> sim(bn * bm, -1.0f);
        auto simAt = [&](size_t x, size_t y) -> float {
            float& s = sim[x * bm + y];
            if (s < 0.0f) s = (float)TextLineSimilarity(la[(size_t)xs[x]], lb[(size_t)ys[y]], allowToken);
            return s;
        };
        const size_t aw = bm + 1;
        std::vector<double> f((bn + 1) * aw, 0.0);
        std::vector<uint8_t> ad((bn + 1) * aw, 0);  // 1=上(A独有) 2=左(B独有) 3=对角(配对)
        for (size_t x = 1; x <= bn; ++x) {
            f[x * aw] = f[(x - 1) * aw];
            ad[x * aw] = 1;
        }
        for (size_t y = 1; y <= bm; ++y) {
            f[y] = f[y - 1];
            ad[y] = 2;
        }
        for (size_t x = 1; x <= bn; ++x) {
            for (size_t y = 1; y <= bm; ++y) {
                double best = f[(x - 1) * aw + y];
                uint8_t d = 1;
                const double left = f[x * aw + y - 1];
                if (left > best) {
                    best = left;
                    d = 2;
                }
                const float s = simAt(x - 1, y - 1);
                if (s >= kSimThreshold) {
                    const double diag = f[(x - 1) * aw + y - 1] + (double)s;
                    if (diag >= best) {
                        best = diag;
                        d = 3;
                    }
                }
                f[x * aw + y] = best;
                ad[x * aw + y] = d;
            }
        }
        std::vector<TextDiffRow> rev;
        size_t x = bn, y = bm;
        while (x > 0 || y > 0) {
            if (x > 0 && y > 0 && ad[x * aw + y] == 3) {
                rev.push_back(TextDiffRow{xs[x - 1], ys[y - 1], 1});
                --x;
                --y;
            } else if (x > 0 && (y == 0 || ad[x * aw + y] == 1)) {
                rev.push_back(TextDiffRow{xs[x - 1], -1, 2});
                --x;
            } else {
                rev.push_back(TextDiffRow{-1, ys[y - 1], 3});
                --y;
            }
        }
        std::reverse(rev.begin(), rev.end());
        rows.insert(rows.end(), rev.begin(), rev.end());
    };

    // ---- 通用：对一串两侧行下标按 id 数组做块内 LCS，输出保序操作序列 ----
    // 返回 false 表示块过大（超出 kMaxAlignCells），调用方退化为兜底策略
    auto lcsOnIds = [](const std::vector<int>& ka, const std::vector<int>& kb, std::vector<DiffOp>& seg) -> bool {
        seg.clear();
        const size_t bn = ka.size();
        const size_t bm = kb.size();
        if (bn == 0 && bm == 0) return true;
        if ((double)bn * (double)bm > kMaxAlignCells) return false;
        const size_t w2 = bm + 1;
        std::vector<uint16_t> l2((bn + 1) * w2, 0);
        std::vector<uint8_t> d2((bn + 1) * w2, 0);
        for (size_t i = 1; i <= bn; ++i) {
            for (size_t j = 1; j <= bm; ++j) {
                const size_t cur = i * w2 + j;
                if (ka[i - 1] == kb[j - 1]) {
                    l2[cur] = (uint16_t)(l2[(i - 1) * w2 + j - 1] + 1);
                    d2[cur] = 0;
                } else if (l2[(i - 1) * w2 + j] >= l2[cur - 1]) {
                    l2[cur] = l2[(i - 1) * w2 + j];
                    d2[cur] = 1;
                } else {
                    l2[cur] = l2[cur - 1];
                    d2[cur] = 2;
                }
            }
        }
        size_t i = bn, j = bm;
        while (i > 0 || j > 0) {
            if (i > 0 && j > 0 && d2[i * w2 + j] == 0) {
                seg.push_back(DiffOp{0, (int)(i - 1), (int)(j - 1)});
                --i;
                --j;
            } else if (j > 0 && (i == 0 || d2[i * w2 + j] == 2)) {
                seg.push_back(DiffOp{2, -1, (int)(j - 1)});
                --j;
            } else {
                seg.push_back(DiffOp{1, (int)(i - 1), -1});
                --i;
            }
        }
        std::reverse(seg.begin(), seg.end());
        return true;
    };

    // ---- 第 2.5 层：按行首键（归一化前几个 token）保序配对 ----
    // 表格行的身份在开头几列（行号 / 代码），后面列数增减、数值全变仍能对上行
    auto keyAlign = [&](const std::vector<int>& xs, const std::vector<int>& ys) {
        if (xs.empty() && ys.empty()) return;
        if (xs.empty()) {
            for (size_t k = 0; k < ys.size(); ++k) insRow(ys[k]);
            return;
        }
        if (ys.empty()) {
            for (size_t k = 0; k < xs.size(); ++k) delRow(xs[k]);
            return;
        }
        std::map<std::wstring, int> kpool;
        auto kid = [&kpool](const std::wstring& s) {
            auto it = kpool.find(s);
            if (it == kpool.end()) it = kpool.emplace(s, (int)kpool.size()).first;
            return it->second;
        };
        std::vector<int> ka(xs.size()), kb(ys.size());
        for (size_t k = 0; k < xs.size(); ++k) ka[k] = kid(keyA[(size_t)xs[k]]);
        for (size_t k = 0; k < ys.size(); ++k) kb[k] = kid(keyB[(size_t)ys[k]]);
        std::vector<DiffOp> seg;
        if (!lcsOnIds(ka, kb, seg)) {
            simAlign(xs, ys);
            return;
        }
        std::vector<int> subA, subB;
        auto flushSub = [&]() {
            if (subA.empty() && subB.empty()) return;
            simAlign(subA, subB);
            subA.clear();
            subB.clear();
        };
        for (const DiffOp& op : seg) {
            if (op.kind == 0) {
                flushSub();
                pairRow(xs[(size_t)op.aIdx], ys[(size_t)op.bIdx]);
            } else if (op.kind == 1) {
                subA.push_back(xs[(size_t)op.aIdx]);
            } else {
                subB.push_back(ys[(size_t)op.bIdx]);
            }
        }
        flushSub();
    };

    // ---- 第二层：替换块内按「归一化行」做 LCS，结构相同的行配成「修改」 ----
    // 归一化相等的多行按原顺序保序配对；剩余落单行交给第 2.5 层行首键，最后相似度兜底
    auto emitBlock = [&](const std::vector<int>& blkA, const std::vector<int>& blkB) {
        const size_t bn = blkA.size();
        const size_t bm = blkB.size();
        if (bn == 0 && bm == 0) return;
        if (bn == 0) {
            for (size_t k = 0; k < bm; ++k) insRow(blkB[k]);
            return;
        }
        if (bm == 0) {
            for (size_t k = 0; k < bn; ++k) delRow(blkA[k]);
            return;
        }
        // 归一化行 intern（块内小池）
        std::map<std::wstring, int> npool;
        auto nid = [&npool](const std::wstring& s) {
            auto it = npool.find(s);
            if (it == npool.end()) it = npool.emplace(s, (int)npool.size()).first;
            return it->second;
        };
        std::vector<int> na(bn), nb(bm);
        for (size_t k = 0; k < bn; ++k) na[k] = nid(nlA[(size_t)blkA[k]]);
        for (size_t k = 0; k < bm; ++k) nb[k] = nid(nlB[(size_t)blkB[k]]);

        std::vector<DiffOp> seg;
        if (!lcsOnIds(na, nb, seg)) {
            simAlign(blkA, blkB);
            return;
        }
        // 归一化相等直接配对；连续的落单行收集成小段交给第 2.5 层
        std::vector<int> subA, subB;
        auto flushSub = [&]() {
            if (subA.empty() && subB.empty()) return;
            keyAlign(subA, subB);
            subA.clear();
            subB.clear();
        };
        for (const DiffOp& op : seg) {
            if (op.kind == 0) {
                flushSub();
                pairRow(blkA[(size_t)op.aIdx], blkB[(size_t)op.bIdx]);
            } else if (op.kind == 1) {
                subA.push_back(blkA[(size_t)op.aIdx]);
            } else {
                subB.push_back(blkB[(size_t)op.bIdx]);
            }
        }
        flushSub();
    };

    // 相邻的「A 独有 + B 独有」合并为替换块处理
    std::vector<int> blkA, blkB;
    auto flush = [&]() {
        emitBlock(blkA, blkB);
        blkA.clear();
        blkB.clear();
    };
    for (const DiffOp& op : ops) {
        if (op.kind == 0) {
            flush();
            sameRow(op.aIdx, op.bIdx);
        } else if (op.kind == 1) {
            blkA.push_back(op.aIdx);
        } else {
            blkB.push_back(op.bIdx);
        }
    }
    flush();
    return true;
}

}  // namespace dpk
