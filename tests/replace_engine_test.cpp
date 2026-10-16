// 替换引擎等价性测试(#129):
//   1) 内嵌用例: 新引擎(字面量快路径/单遍正则) 与 std::regex_replace(format_default)
//      逐字节等价 + 命中计数正确性 —— CI 必跑(无外部依赖)
//   2) 真实数据用例: 设环境变量 GHD2C_TEST_JSON=<localization.json> GHD2C_TEST_JS=<js 文件>,
//      逐条目(main/renderer/select.replace)跑新旧引擎对比 —— 本地验证用, 未设则 SKIP
// 测试框架: GoogleTest(third_party/googletest)
// 构建: cmake --build . --target replace_engine_test;  运行: ctest / 直接运行
#include "utils/replace_engine.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace {

// 可移植环境变量读取: MSVC 下 std::getenv 触发 C4996(CI 以告警为错误), 改用 _dupenv_s
std::string GetEnvVar(const char* name) {
#if defined(_MSC_VER)
    char* buf = nullptr;
    size_t len = 0;
    if(_dupenv_s(&buf, &len, name) != 0 || buf == nullptr) {
        return std::string();
    }
    std::string value(buf);
    std::free(buf);
    return value;
#else
    const char* v = std::getenv(name);
    return v != nullptr ? std::string(v) : std::string();
#endif
}

// 对单条 (pattern, replacement) 走主程序同等分派逻辑, 与 regex_replace 基准逐字节对比
// 分派逻辑与 GitHubDesktop2Chinese.cpp 一致: 纯字面量且替换文本无 $ 走快路径, 否则单遍正则
void CompareEntry(const std::string& subject, const std::string& pattern, const std::string& replacement) {
    SCOPED_TRACE("pattern=" + pattern);
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
    EXPECT_EQ(s, expected);
    // 命中计数: 基准在原串上用 sregex_iterator 数(不是在替换后的 expected 上)
    size_t expected_hits = 0;
    const auto end_it = std::sregex_iterator();
    for(auto it = std::sregex_iterator(subject.begin(), subject.end(), re); it != end_it; ++it) {
        expected_hits++;
    }
    EXPECT_EQ(hits, expected_hits);
}

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

} // namespace

TEST(ReplaceEngine, EmbeddedCases) {
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
}

// 真实数据对比: 逐条目新旧引擎输出全等(环境变量驱动, 未设则跳过; 不进 CI 断言)
TEST(ReplaceEngine, RealDataFromEnv) {
    const std::string json_path = GetEnvVar("GHD2C_TEST_JSON");
    const std::string js_path = GetEnvVar("GHD2C_TEST_JS");
    if(json_path.empty() || js_path.empty()) {
        GTEST_SKIP() << "未设置 GHD2C_TEST_JSON / GHD2C_TEST_JS, 跳过真实数据对比";
    }
    std::ifstream js_in(js_path, std::ios::binary);
    ASSERT_TRUE(js_in) << "无法打开 js 文件: " << js_path;
    std::string js((std::istreambuf_iterator<char>(js_in)), std::istreambuf_iterator<char>());
    std::cout << "js 大小: " << (js.size() / 1048576.0) << " MB" << std::endl;

    const auto pairs = ExtractPairs(json_path);
    std::cout << "提取映射条目: " << pairs.size() << std::endl;
    ASSERT_FALSE(pairs.empty()) << "未从 " << json_path << " 提取到任何映射条目";

    size_t plain_count = 0;
    int mismatches = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for(const auto& entry : pairs) {
        const std::string& pattern = entry.first;
        const std::string& replacement = entry.second;
        if(pattern.empty() || pattern == "\"\"") {
            continue;
        }
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
        // 大文本失败信息截断(避免 1MB×2 输出刷屏)
        if(s != expected) {
            mismatches++;
            ADD_FAILURE() << "条目不等价: " << pattern.substr(0, 80);
            if(mismatches >= 5) {
                std::cout << "不等价条目过多, 截断输出" << std::endl;
                break;
            }
        }
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "真实条目等价完成: 字面量快路径 " << plain_count << " 条, 耗时 " << ms << " ms" << std::endl;
}
