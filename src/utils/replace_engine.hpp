#pragma once
// ── 替换引擎(#129): regex_replace 整串重建 O(P×N) → 单遍构建 + 字面量快路径 ──
// 从 TuPigGHD2Chinese.cpp 抽出以便单元测试; 语义契约:
//   ReplaceLiteralInPlace / ReplaceRegexInPlace 与
//   std::regex_replace(s, pattern, fmt, format_default) 输出逐字节一致
#include <regex>
#include <string>

namespace utils {

// 判断 pattern 是否为纯字面量(无 ECMAScript 元字符): 是则跳过 regex 构造走 string::find,
// 在所有标准库实现上等速(memchr 级), 一并消除 libstdc++ regex 的 12.4× 平台性能方差(#129)
inline bool IsPlainLiteral(const std::string& pattern) {
    if(pattern.empty()) {
        return false;
    }
    static const std::string kRegexMetachars = "^$\\.*+?()[]{}|";
    return pattern.find_first_of(kRegexMetachars) == std::string::npos;
}

// 字面量快路径: string::find 单遍替换, 返回命中次数; 零命中不改动 s(无整串复制)
// 前置条件: needle 为纯字面量且 replacement 不含 '$'
inline size_t ReplaceLiteralInPlace(std::string& s, const std::string& needle, const std::string& replacement) {
    size_t hits = 0;
    size_t last = 0;
    size_t pos = 0;
    std::string out;
    out.reserve(s.size() + 16);
    while((pos = s.find(needle, pos)) != std::string::npos) {
        out.append(s, last, pos - last);
        out.append(replacement);
        last = pos + needle.size();
        pos = last; // 不重扫替换文本, 与 regex_replace 非重叠语义一致
        hits++;
    }
    if(hits == 0) {
        return 0;
    }
    out.append(s, last, s.size() - last);
    s = std::move(out);
    return hits;
}

// 正则单遍替换: sregex_iterator 构建输出并 reserve, 消除 regex_replace 每条目一次的整串重建;
// format 语义与 std::regex_replace(format_default) 一致($&/$n/$` 展开)
inline size_t ReplaceRegexInPlace(std::string& s, const std::regex& pattern, const std::string& replacement) {
    size_t hits = 0;
    size_t last = 0;
    std::string out;
    out.reserve(s.size() + 16);
    const auto end_it = std::sregex_iterator();
    for(auto it = std::sregex_iterator(s.begin(), s.end(), pattern); it != end_it; ++it) {
        const std::smatch& m = *it;
        const auto mpos = static_cast<size_t>(m.position());
        out.append(s, last, mpos - last);
        m.format(std::back_inserter(out), replacement, std::regex_constants::format_default);
        last = mpos + static_cast<size_t>(m.length());
        hits++;
    }
    if(hits == 0) {
        return 0;
    }
    out.append(s, last, s.size() - last);
    s = std::move(out);
    return hits;
}

} // namespace utils
