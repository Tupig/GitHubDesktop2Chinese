// Version 解析与比较的单元测试(#123)
// 测试框架: GoogleTest(随仓库 vendored 在 third_party/googletest)
// 运行: ctest --output-on-failure  或直接执行 version_test
#include "version/Version.hpp"

#include <gtest/gtest.h>

#include <string>

using versionparse::Version;
// Status 是 Version 的嵌套枚举, 不能直接 using versionparse::Status
using Status = versionparse::Version::Status;

namespace {

std::string StatusName(Status s) {
    switch (s) {
        case Status::Dev: return "Dev";
        case Status::Beta: return "Beta";
        default: return "Release";
    }
}

struct ParseCase {
    const char* input;
    bool valid;
    int major;
    int minor;
    int revision;
    int betaversion;
    Status status;
};

// 表驱动用例(文件级聚合数组)
const ParseCase kCases[] = {
    // 常规三段式(v/V 前缀、纯数字)
    {"1.2", true, 1, 2, 0, 0, Status::Release},
    {"v1.2.30", true, 1, 2, 30, 0, Status::Release},
    {"V3.6.6", true, 3, 6, 6, 0, Status::Release},
    // alpha/dev/beta 后缀(大小写不敏感、'_' 分隔符等价)
    {"1.2.3-alpha.456", true, 1, 2, 3, 456, Status::Dev},
    {"1.2.3-dev.5", true, 1, 2, 3, 5, Status::Dev},
    {"1.2.3-ALPHA.7", true, 1, 2, 3, 7, Status::Dev},
    {"1.2.3-beta.7", true, 1, 2, 3, 7, Status::Beta},
    {"1.2.3_Beta.7", true, 1, 2, 3, 7, Status::Beta},
    // 第 4 段数字: 非零升 Beta, 零保持 Release
    {"1.2.3.4", true, 1, 2, 3, 4, Status::Beta},
    {"1.2.3.0", true, 1, 2, 3, 0, Status::Release},
    // 未知后缀: 丢弃后缀保留已解析部分, 后缀中数字不并入版本号(#120)
    {"1.2-rc1", true, 1, 2, 0, 0, Status::Release},
    {"1.2.3-4", true, 1, 2, 3, 0, Status::Release},
    {"1.2.3-rc.2", true, 1, 2, 3, 0, Status::Release},
    // 无分隔符垃圾字符: 终止当前段解析并保留已识别部分, 其后数字不并入版本号(#175)
    {"1.2abc3", true, 1, 2, 0, 0, Status::Release},
    {"1.2.3x4", true, 1, 2, 3, 0, Status::Release},
    {"1.2.3rc4", true, 1, 2, 3, 0, Status::Release},
    {"1.2.x3", true, 1, 2, 0, 0, Status::Release},
    {"1.x2", false, 0, 0, 0, 0, Status::Release},
    // 组件上限 99999: 累积后检查(边界恰为上限值, #122 A2); 至少要读出 major.minor 两段
    {"99999.1", true, 99999, 1, 0, 0, Status::Release},
    {"1.99999", true, 1, 99999, 0, 0, Status::Release},
    {"99999", false, 0, 0, 0, 0, Status::Release},
    {"100000", false, 0, 0, 0, 0, Status::Release},
    {"999999", false, 0, 0, 0, 0, Status::Release},
    {"1.100000", false, 0, 0, 0, 0, Status::Release},
    // 无效输入
    {"", false, 0, 0, 0, 0, Status::Release},
    {"abc", false, 0, 0, 0, 0, Status::Release},
};

} // namespace

TEST(VersionParse, TableDriven) {
    for (const auto& c : kCases) {
        SCOPED_TRACE(std::string("Parse(\"") + c.input + "\")");
        Version v(c.input);
        EXPECT_EQ(bool(v), c.valid);
        if (!c.valid) {
            continue;
        }
        EXPECT_EQ(v.major, c.major);
        EXPECT_EQ(v.minor, c.minor);
        EXPECT_EQ(v.revision, c.revision);
        EXPECT_EQ(v.betaversion, c.betaversion);
        EXPECT_EQ(StatusName(v.status), StatusName(c.status));
    }
}

// 比较运算符: 正式版与测试版序、数值比较、相等语义
TEST(VersionCompare, OrderingAndEquality) {
    EXPECT_GT(Version("1.13.1"), Version("1.13.1-beta.1"));
    EXPECT_LT(Version("1.2"), Version("1.10"));
    EXPECT_GT(Version("1.2.3-beta.2"), Version("1.2.3-beta.1"));
    EXPECT_GT(Version("1.2.3-beta.1"), Version("1.2.3-alpha.9"));
    EXPECT_TRUE(Version("1.2.3") == Version("1.2.3"));
    EXPECT_TRUE(Version("1.2.3-beta.1") == Version("1.2.3-beta.1"));
    EXPECT_NE(Version("1.2.3-beta.1"), Version("1.2.3"));
    EXPECT_LE(Version("1.2"), Version("1.2"));
}
