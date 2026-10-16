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

// ReDoS 启发式(#128/#240/#271): 规则文本与 tools/auto-maintain/regex-blacklist.json 的
// redos_heuristic.regex **逐字一致**（由 tools/ci/check-localization.py 校验一致性），
// 与 CI（check-localization.py）及维护工具（check-invalid.js）共用同一语义。
// 覆盖形态：组内含量词(+/*)、区间量词({n,})或交替分支(|)，且组结束（可含嵌套括号）后紧跟量词(+/*/{)。
// 该规则为旧 `\(.*[+*].*\)\s*[+*{]` 的**严格超集**：旧命中集不变，新增 {n,} 与 | 形态。
// 保守语义：宁可漏汉化不可假死。
// 历史教训(#271)：此前为手写逐字符扫描器，与 CI 口径分歧导致 ((a+))+ / (a(b+))+ / (?:(a+))+
// 等在运行期漏检（CI 会拦），而 (a{10,})+ / (a|a)+ 两侧皆漏；改为同一规则文本后消除该分歧。
inline constexpr const char kRedosHeuristicRegex[] =
    R"(\(.*(?:[+*]|\{[0-9]+,\}|\|).*\)\s*[+*{])";

inline bool LooksLikeNestedQuantifier(const std::string& pat) {
    static const std::regex kRe(kRedosHeuristicRegex, std::regex::optimize);
    return std::regex_search(pat, kRe);
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
