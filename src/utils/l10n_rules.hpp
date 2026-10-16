#ifndef UTILS_L10N_RULES_HPP
#define UTILS_L10N_RULES_HPP

// 本地化映射的纯规则逻辑（自 src/TuPigGHD2Chinese.cpp 原样抽出，便于单元测试，#267）：
//   $ 转义 / ReDoS 静态启发式与安全拒绝判定 / 第三参数 #{n} 占位符回填
// 抽出仅改变可见性（static → utils 命名空间 inline），函数体与语义逐字保持不变；
// 调用方（主程序）通过 using 声明保持原调用形式不变。

#include <cstddef>
#include <optional>
#include <regex>
#include <string>

#include <spdlog/spdlog.h>

namespace utils {

// 转义 std::regex_replace format 串中的 $ (否则 $&/$`/$n 会被展开或吞掉): 用于把捕获文本安全回填到模板
inline auto EscapeDollar(const std::string& s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for(char c : s) {
        if(c == '$') out += "$$";
        else out += c;
    }
    return out;
}

// 占位符回填结果（原 PlaceholderFillResult）
enum PlaceholderFillResult {
    kPlaceholderPatternMiss = 0, // 第三参数在全文未命中 → 调用方跳过该条
    kPlaceholderLeftover = 1,    // 回填后仍残留 #{n} → 调用方跳过该条
    kPlaceholderFilled = 2,      // 回填成功
    kPlaceholderUnsafe = 3,      // 第三参数命中 ReDoS 防护 → 调用方跳过该条(#128)
};

// ReDoS 嵌套量词静态启发式(#128): 与 CI regex-blacklist.json 的
// \([^()]*[+*][^()]*\)\s*[+*{] 语义对齐 —— 无嵌套括号的组内含 +/* 字符
// 且组结束后紧跟量词字符(+ * {)即疑似灾难性回溯(保守: 转义量词同样命中, 与 CI 侧一致)
inline bool LooksLikeNestedQuantifier(const std::string& pat) {
    const size_t n = pat.size();
    for(size_t i = 0; i < n; i++) {
        if(pat[i] != '(') {
            continue;
        }
        size_t j = i + 1;
        bool group_has_quantifier = false;
        while(j < n && pat[j] != ')') {
            if(pat[j] == '+' || pat[j] == '*') {
                group_has_quantifier = true;
            }
            if(pat[j] == '(') {
                // 嵌套组也视为疑似(#240): 原实现遇 '(' 即跳过, 放行了 ((a)*)* 类可致
                // std::regex 灾难性回溯的形态; 保守拒绝(宁可漏汉化不可假死)
                group_has_quantifier = true;
            }
            j++;
        }
        // 未闭合 / 组内无量词与嵌套括号: 不构成启发式形态
        if(j >= n || pat[j] != ')' || !group_has_quantifier) {
            continue;
        }
        size_t k = j + 1; // \s*
        while(k < n && (pat[k] == ' ' || pat[k] == '\t' || pat[k] == '\n' ||
                        pat[k] == '\r' || pat[k] == '\f' || pat[k] == '\v')) {
            k++;
        }
        if(k < n && (pat[k] == '+' || pat[k] == '*' || pat[k] == '{')) {
            return true;
        }
        i = j;
    }
    return false;
}

// ReDoS 防护(#128): 映射来自远程下载或本地 -j, 无哈希/签名校验(仅 TLS),
// std::regex 回溯引擎无超时, 嵌套量词模式可致 CPU 100% 假死 —— 编译前静态拒绝
// 返回 nullopt 表示安全; 有值为拒绝原因(调用方跳过该条并告警, 不中止整个文件)
inline std::optional<std::string> PatternSafetyRejection(const std::string& pat) {
    constexpr size_t kMaxPatternLen = 4096;
    if(pat.size() > kMaxPatternLen) {
        return "长度超过 " + std::to_string(kMaxPatternLen) + " 上限";
    }
    if(LooksLikeNestedQuantifier(pat)) {
        return "疑似灾难性回溯(嵌套量词)";
    }
    return std::nullopt;
}

// 超长替换文本防护(#128): 防止超长 replacement 经千条量级映射放大造成输出膨胀
inline std::optional<std::string> ReplacementSafetyRejection(const std::string& rep) {
    constexpr size_t kMaxReplacementLen = 16384;
    if(rep.size() > kMaxReplacementLen) {
        return "替换文本超过 " + std::to_string(kMaxReplacementLen) + " 上限";
    }
    return std::nullopt;
}

// 在全文查找第三参数(pattern3)首个匹配, 用捕获组逐字面回填 translation 中的 #{%d}; 返回回填状态
inline int FillPlaceholders(const std::string& js_str, const std::string& pattern3_str, std::string& translation) {
    if(const auto reject = PatternSafetyRejection(pattern3_str)) {
        spdlog::warn("跳过疑似 ReDoS 的第三参数正则({}): {}", *reject, pattern3_str);
        return kPlaceholderUnsafe;
    }
    std::regex pattern3(pattern3_str, std::regex::optimize);
    std::sregex_iterator it = std::sregex_iterator(js_str.begin(), js_str.end(), pattern3);
    if(it == std::sregex_iterator()) {
        return kPlaceholderPatternMiss;
    }
    const std::smatch& match = *it;
    for(size_t i = 1; i < match.size(); i++) {
        if(!match[i].matched) continue;
        // 逐字面查找 #{i} 并回填; 捕获文本先转义 $, 防止其作为 format 串时 $&/$n 被展开
        const std::string ph = "#{" + std::to_string(i) + "}";
        const std::string val = EscapeDollar(match[i].str());
        size_t pos = 0;
        while((pos = translation.find(ph, pos)) != std::string::npos) {
            translation.replace(pos, ph.size(), val);
            pos += val.size();
        }
    }
    // 捕获组数不足导致的残留占位符若写入 JS 会造成语法破坏, 调用方必须跳过该条
    static const std::regex leftover_ph(R"(#\{\d+\})", std::regex::optimize);
    if(std::regex_search(translation, leftover_ph)) {
        return kPlaceholderLeftover;
    }
    return kPlaceholderFilled;
}

} // namespace utils

#endif // !UTILS_L10N_RULES_HPP
