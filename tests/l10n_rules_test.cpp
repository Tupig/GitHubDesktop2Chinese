// 映射纯规则单元测试(#267): $ 转义 / ReDoS 静态启发式与安全拒绝判定 / 第三参数占位符回填
// 测试框架: GoogleTest(third_party/googletest)
// 被测: src/utils/l10n_rules.hpp（原 src/TuPigGHD2Chinese.cpp 内 static 实现，抽出以便单测）
// 构建: cmake --build . --target l10n_rules_test;  运行: ctest
#include "utils/l10n_rules.hpp"

#include <gtest/gtest.h>

#include <string>

// ── EscapeDollar: std::regex_replace format 串的 $ 转义 ──
TEST(EscapeDollar, NoDollarUnchanged) {
    EXPECT_EQ(utils::EscapeDollar("plain text"), "plain text");
    EXPECT_EQ(utils::EscapeDollar(""), "");
}

TEST(EscapeDollar, SingleDollarDoubled) {
    EXPECT_EQ(utils::EscapeDollar("$"), "$$");
}

TEST(EscapeDollar, EveryDollarDoubled) {
    EXPECT_EQ(utils::EscapeDollar("a$b$c"), "a$$b$$c");
    EXPECT_EQ(utils::EscapeDollar("$&$1$`"), "$$&$$1$$`");
    EXPECT_EQ(utils::EscapeDollar("$$"), "$$$$");
}

// ── LooksLikeNestedQuantifier: ReDoS 静态启发式 ──
TEST(NestedQuantifier, GroupWithQuantifierFollowedByQuantifier) {
    EXPECT_TRUE(utils::LooksLikeNestedQuantifier("(a+)+"));
    EXPECT_TRUE(utils::LooksLikeNestedQuantifier("(a*)*"));
    EXPECT_TRUE(utils::LooksLikeNestedQuantifier("(a+){2}"));
}

TEST(NestedQuantifier, WhitespaceBetweenGroupAndQuantifierStillMatches) {
    EXPECT_TRUE(utils::LooksLikeNestedQuantifier("(a+)\t+"));
    EXPECT_TRUE(utils::LooksLikeNestedQuantifier("(a*)  *"));
}

TEST(NestedQuantifier, NestedGroupTreatedAsSuspicious) {
    // #240/#271: 嵌套组保守拒绝（原实现遇 '(' 跳过, 放行 ((a)*)* 类形态）
    EXPECT_TRUE(utils::LooksLikeNestedQuantifier("((a)*)*"));
    EXPECT_TRUE(utils::LooksLikeNestedQuantifier("((a+))+"));
    EXPECT_TRUE(utils::LooksLikeNestedQuantifier("(a(b+))+"));
    EXPECT_TRUE(utils::LooksLikeNestedQuantifier("(?:(a+))+"));
    EXPECT_TRUE(utils::LooksLikeNestedQuantifier("((\\w+))+"));
}

TEST(NestedQuantifier, IntervalQuantifierInsideGroupAlsoSuspicious) {
    // #271: 组内区间量词 {n,} + 组后量词 —— 修复前 C++ 与 CI 双双漏检（现实灾难性回溯）
    EXPECT_TRUE(utils::LooksLikeNestedQuantifier("(a{10,})+"));
    EXPECT_TRUE(utils::LooksLikeNestedQuantifier("(ab{1,})*"));
}

TEST(NestedQuantifier, AlternationInsideGroupAlsoSuspicious) {
    // #271: 组内交替 + 组后量词（(a|a)+ 类）—— 保守一律拒绝
    EXPECT_TRUE(utils::LooksLikeNestedQuantifier("(a|a)+"));
    EXPECT_TRUE(utils::LooksLikeNestedQuantifier("(foo|bar)*"));
}

TEST(NestedQuantifier, RuleTextIsMirroredFromSingleSourceOfTruth) {
    // 规则文本与 tools/auto-maintain/regex-blacklist.json 的 redos_heuristic.regex 逐字一致，
    // 该一致性由 tools/ci/check-localization.py 强制校验（#271）；此处仅锁常量非空且可编译。
    EXPECT_NE(std::string(utils::kRedosHeuristicRegex).size(), 0u);
    EXPECT_TRUE(utils::LooksLikeNestedQuantifier("(a+)+")); // 常量可被实际使用
}

TEST(NestedQuantifier, EscapedFormsAlsoMatch) {
    // 保守语义: 转义量词同样命中（与 CI 侧 regex-blacklist 一致）
    EXPECT_TRUE(utils::LooksLikeNestedQuantifier("\\(a+\\)+"));
}

TEST(NestedQuantifier, PlainPatternsAreNotSuspicious) {
    EXPECT_FALSE(utils::LooksLikeNestedQuantifier("abc"));
    EXPECT_FALSE(utils::LooksLikeNestedQuantifier("[a-z]+"));
    EXPECT_FALSE(utils::LooksLikeNestedQuantifier("(abc)"));
    EXPECT_FALSE(utils::LooksLikeNestedQuantifier("(a+)x"));      // 组后无紧跟量词
    EXPECT_FALSE(utils::LooksLikeNestedQuantifier("(a+"));        // 未闭合
    EXPECT_FALSE(utils::LooksLikeNestedQuantifier(""));
}

// ── PatternSafetyRejection: 长度上限 + 启发式 ──
TEST(PatternSafetyRejection, SafePatternReturnsNullopt) {
    EXPECT_FALSE(utils::PatternSafetyRejection("\\d+ hits").has_value());
}

TEST(PatternSafetyRejection, LengthBoundaryIsInclusive) {
    EXPECT_FALSE(utils::PatternSafetyRejection(std::string(4096, 'a')).has_value());
    const auto rejected = utils::PatternSafetyRejection(std::string(4097, 'a'));
    ASSERT_TRUE(rejected.has_value());
    EXPECT_NE(rejected->find("4096"), std::string::npos);
}

TEST(PatternSafetyRejection, CatastrophicBacktrackingRejected) {
    const auto rejected = utils::PatternSafetyRejection("(a+)+x");
    ASSERT_TRUE(rejected.has_value());
    EXPECT_NE(rejected->find("回溯"), std::string::npos);
}

TEST(PatternSafetyRejection, IntervalAndAlternationFormsAlsoRejected) {
    // #271: 修复前 (a{10,})+ / (a|a)+ 两侧皆漏检（CI 放行 + 运行期不拦）
    EXPECT_TRUE(utils::PatternSafetyRejection("(a{10,})+").has_value());
    EXPECT_TRUE(utils::PatternSafetyRejection("(a|a)+").has_value());
}

TEST(PatternSafetyRejection, LengthCheckedBeforeHeuristic) {
    // 超长且含嵌套量词时优先报长度上限
    const auto rejected = utils::PatternSafetyRejection(std::string(5000, 'a') + "(b+)+");
    ASSERT_TRUE(rejected.has_value());
    EXPECT_NE(rejected->find("长度超过"), std::string::npos);
}

// ── ReplacementSafetyRejection: 替换文本长度上限 ──
TEST(ReplacementSafetyRejection, BoundaryIsInclusive) {
    EXPECT_FALSE(utils::ReplacementSafetyRejection(std::string(16384, 'x')).has_value());
    const auto rejected = utils::ReplacementSafetyRejection(std::string(16385, 'x'));
    ASSERT_TRUE(rejected.has_value());
    EXPECT_NE(rejected->find("16384"), std::string::npos);
}

// ── FillPlaceholders: 第三参数捕获组回填 #{n} ──
TEST(FillPlaceholders, FillsCaptureGroupsInOrder) {
    std::string translation = "前缀 #{1} 中缀 #{2} 后缀";
    const int r = utils::FillPlaceholders("key=value", "(\\w+)=(\\w+)", translation);
    EXPECT_EQ(r, utils::kPlaceholderFilled);
    EXPECT_EQ(translation, "前缀 key 中缀 value 后缀");
}

TEST(FillPlaceholders, RepeatedPlaceholderFilledEverywhere) {
    std::string translation = "#{1}-#{1}";
    EXPECT_EQ(utils::FillPlaceholders("v1", "v(\\d+)", translation), utils::kPlaceholderFilled);
    EXPECT_EQ(translation, "1-1");
}

TEST(FillPlaceholders, CapturedDollarIsEscaped) {
    // 捕获文本含 $ 时须转义为 $$, 否则作为 format 串会被展开
    std::string translation = "#{1}";
    EXPECT_EQ(utils::FillPlaceholders("cost $5", "(\\$\\d)", translation), utils::kPlaceholderFilled);
    EXPECT_EQ(translation, "$$5");
}

TEST(FillPlaceholders, NoMatchReportsPatternMiss) {
    std::string translation = "#{1}";
    EXPECT_EQ(utils::FillPlaceholders("nothing here", "(\\d+)", translation), utils::kPlaceholderPatternMiss);
    EXPECT_EQ(translation, "#{1}"); // 未命中不改动
}

TEST(FillPlaceholders, LeftoverPlaceholderReportsLeftover) {
    std::string translation = "#{1}#{9}";
    EXPECT_EQ(utils::FillPlaceholders("v1", "v(\\d+)", translation), utils::kPlaceholderLeftover);
    EXPECT_EQ(translation, "1#{9}");
}

TEST(FillPlaceholders, UnmatchedOptionalGroupLeavesItsPlaceholderAsLeftover) {
    std::string translation = "#{1}#{2}";
    // 第二组可选且未参与匹配 → 其占位符残留 → leftover
    EXPECT_EQ(utils::FillPlaceholders("ab", "a(b)(c)?", translation), utils::kPlaceholderLeftover);
    EXPECT_EQ(translation, "b#{2}");
}

TEST(FillPlaceholders, RedosPatternReportsUnsafe) {
    std::string translation = "#{1}";
    EXPECT_EQ(utils::FillPlaceholders("aaaa", "(a+)+b", translation), utils::kPlaceholderUnsafe);
    EXPECT_EQ(translation, "#{1}"); // 被拒时不改动
}

TEST(FillPlaceholders, NoPlaceholderInTranslationStillFilled) {
    std::string translation = "固定译文";
    EXPECT_EQ(utils::FillPlaceholders("v1", "v(\\d+)", translation), utils::kPlaceholderFilled);
    EXPECT_EQ(translation, "固定译文");
}
