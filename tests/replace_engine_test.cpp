// 替换引擎等价性测试(#129):
//   1) 内嵌用例: 新引擎(字面量快路径/单遍正则) 与 std::regex_replace(format_default)
//      逐字节等价 + 命中计数正确性 —— CI 必跑(无外部依赖)
//   2) 真实数据用例: 可选 argv[1]=localization.json argv[2]=js 文件,
//      逐条目(main/renderer/select.replace)跑新旧引擎对比 —— 本地验证用, 不进 CI 断言
// 构建: cmake --build . --target replace_engine_test;  运行: ctest / 直接运行
#include "utils/replace_engine.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void CheckEqual(const std::string& name, const std::string& actual, const std::string& expected) {
    g_checks++;
    if(actual != expected) {
        g_failures++;
        std::printf("  [FAIL] %s\n    expected: %.80s\n    actual:   %.80s\n",
                    name.c_str(), expected.c_str(), actual.c_str());
    }
}

void CheckTrue(const std::string& name, bool cond) {
    g_checks++;
    if(!cond) {
        g_failures++;
        std::printf("  [FAIL] %s\n", name.c_str());
    }
}

// 对单条 (pattern, replacement) 走主程序同等分派逻辑, 与 regex_replace 基准逐字节对比
// 分派逻辑与 GitHubDesktop2Chinese.cpp 一致: 纯字面量且替换文本无 $ 走快路径, 否则单遍正则
void CompareEntry(const std::string& subject, const std::string& pattern, const std::string& replacement) {
    std::regex re(pattern, std::regex::optimize);
    const std::string expected = std::regex_replace(subject, re, replacement, std::regex_constants::format_default);
    std::string s = subject;
    const bool plain = utils::IsPlainLiteral(pattern) && replacement.find('$') == std::string::npos;
    size_t hits = 0;
    if(plain) {
        hits = utils::ReplaceLiteralInPlace(s, pattern, replacement);
    }
    else {
        hits = utils::ReplaceRegexInPlace(s, re, replacement);
    }
    CheckEqual("输出等价 [" + pattern + "]", s, expected);
    // 命中计数: 基准用 sregex_iterator 数
    size_t expected_hits = 0;
    const auto end_it = std::sregex_iterator();
    for(auto it = std::sregex_iterator(expected.begin(), expected.end(), re); it != end_it; ++it) {
        expected_hits++;
    }
    CheckTrue("命中计数等价 [" + pattern + "]", hits == expected_hits);
}

void RunEmbeddedCases() {
    std::printf("[1] 内嵌等价用例\n");
    // 字面量多命中(快路径)
    CompareEntry("Hello World, Hello GitHub, Hello!", "Hello", "你好");
    // 字面量零命中(不改动原串)
    CompareEntry("abcdef", "xyz", "ZZZ");
    // 字面量命中在开头与结尾
    CompareEntry("abcXXdefXX", "XX", "-");
    // 替换文本包含 needle 自身(防死循环/防重扫)
    CompareEntry("ababab", "ab", "abab");
    // 替换文本包含反向内容
    CompareEntry("aaaa", "aa", "b");
    // 中文 UTF-8 字面量
    CompareEntry("设置\n选项\n设置", "设置", "偏好设置");
    // 正则元字符(走正则路径): \d+ 数字
    CompareEntry("v1.2.3 build 42", "\\d+", "#");
    // 捕获组引用 $1
    CompareEntry("key=value", "(\\w+)=(\\w+)", "$2:$1");
    // $& 整体匹配展开
    CompareEntry("abc def", "[a-z]+", "[$&]");
    // $$ 折叠(format 转义)
    CompareEntry("price", "price", "100$$");
    // 锚点与多行
    CompareEntry("line1\nline2\nline3", "^line", "row");
    // 字符类
    CompareEntry("a.b|c", "[.|]", ",");
    // 可选量词
    CompareEntry("color colour", "colou?r", "COLOUR");
    // 空匹配模式(边界: 与 regex_replace 空匹配推进语义一致)
    CompareEntry("abc", "x*", "-");
    // 命中相邻
    CompareEntry("aaa", "a", "b");
    std::printf("  内嵌用例: %d 项断言, %d 失败\n", g_checks, g_failures);
}

// ── 真实数据对比: 逐条目新旧引擎输出全等 ──
// 简化 JSON 行解析: 仅提取 localization.json 的 ["...", "...", ...] 行(映射条目均单行)
std::vector<std::pair<std::string, std::string>> ExtractPairs(const std::string& path) {
    std::vector<std::pair<std::string, std::string>> pairs;
    std::ifstream in(path);
    std::string line;
    std::string acc;
    while(std::getline(in, line)) {
        acc += line;
        // 行内含两个以上字符串字面量的映射行: 提取前两个非空字符串
        const size_t first = acc.find('"');
        if(first == std::string::npos) { acc.clear(); continue; }
        // 简单状态机提取所有完整字符串
        std::vector<std::string> strs;
        size_t i = 0;
        while(i < acc.size() && strs.size() < 3) {
            if(acc[i] == '"') {
                std::string cur;
                ++i;
                while(i < acc.size() && acc[i] != '"') {
                    if(acc[i] == '\\' && i + 1 < acc.size()) { cur += acc[i]; cur += acc[i + 1]; i += 2; continue; }
                    cur += acc[i];
                    ++i;
                }
                ++i;
                if(!cur.empty()) strs.push_back(cur);
            }
            else {
                ++i;
            }
        }
        if(strs.size() >= 2 && strs[0].size() > 2) {
            pairs.emplace_back(strs[0], strs[1]);
        }
        acc.clear();
    }
    return pairs;
}

int RunRealDataCases(const char* json_path, const char* js_path) {
    std::printf("[2] 真实数据等价用例\n  json: %s\n  js: %s\n", json_path, js_path);
    std::ifstream js_in(js_path, std::ios::binary);
    if(!js_in) {
        std::printf("  [SKIP] 无法打开 js 文件\n");
        return 0;
    }
    std::string js((std::istreambuf_iterator<char>(js_in)), std::istreambuf_iterator<char>());
    std::printf("  js 大小: %.2f MB\n", js.size() / 1048576.0);

    const auto pairs = ExtractPairs(json_path);
    std::printf("  提取映射条目: %zu\n", pairs.size());
    int mismatches = 0;
    size_t plain_count = 0;
    int before_failures = g_failures;
    const auto t0 = std::chrono::steady_clock::now();
    for(const auto& [pattern, replacement] : pairs) {
        if(pattern.empty() || pattern == "\"\"") {
            continue;
        }
        // 与主程序一致的 ReDoS 防护跳过条件由上游保证, 此处直测引擎等价;
        // 非法正则条目跳过(与主程序 invalidcheck 语义无关, 仅测合法条目)
        std::regex re;
        try {
            re.assign(pattern, std::regex::optimize);
        }
        catch(const std::regex_error&) {
            continue;
        }
        const std::string expected = std::regex_replace(js, re, replacement, std::regex_constants::format_default);
        std::string s = js;
        const bool plain = utils::IsPlainLiteral(pattern) && replacement.find('$') == std::string::npos;
        if(plain) {
            plain_count++;
            (void)utils::ReplaceLiteralInPlace(s, pattern, replacement);
        }
        else {
            (void)utils::ReplaceRegexInPlace(s, re, replacement);
        }
        g_checks++;
        if(s != expected) {
            g_failures++;
            mismatches++;
            std::printf("  [FAIL] 条目不等价: %.80s\n", pattern.c_str());
            if(mismatches >= 5) {
                std::printf("  不等价条目过多, 截断输出\n");
                break;
            }
        }
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  真实条目等价: %d 断言 / %d 失败 (字面量快路径 %zu 条), 耗时 %lld ms\n",
                g_checks - 0, g_failures - before_failures, plain_count, static_cast<long long>(ms));
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    RunEmbeddedCases();
    if(argc >= 3) {
        (void)RunRealDataCases(argv[1], argv[2]);
    }
    else {
        std::printf("[2] 真实数据用例: 未提供 argv[1](json) argv[2](js), 跳过(本地可手动运行)\n");
    }
    if(g_failures == 0) {
        std::printf("全部通过: %d 项断言\n", g_checks);
        return 0;
    }
    std::printf("失败: %d / %d 项断言\n", g_failures, g_checks);
    return 1;
}
