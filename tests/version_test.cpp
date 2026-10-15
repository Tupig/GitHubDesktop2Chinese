// Version 解析与比较的表驱动单元测试(#123)
// 无外部测试框架: 断言失败打印用例与期望/实际, 全部通过打印统计并返回 0
// 运行: 直接执行 version_test, 或 ctest --output-on-failure
#include "version/Version.hpp"

#include <cstdio>
#include <string>

using versionparse::Version;
// Status 是 Version 的嵌套枚举, 不能直接 using versionparse::Status
using Status = versionparse::Version::Status;

namespace {

int g_total = 0;
int g_failed = 0;

void Expect(bool cond, const std::string& what) {
    ++g_total;
    if (!cond) {
        ++g_failed;
        std::printf("  [FAIL] %s\n", what.c_str());
    }
}

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

void RunParseCase(const ParseCase& c) {
    Version v(c.input);
    const std::string label = std::string("Parse(\"") + c.input + "\")";
    Expect(bool(v) == c.valid,
           label + " 有效性: 期望 " + (c.valid ? "true" : "false") +
           " 实际 " + (bool(v) ? "true" : "false"));
    if (!c.valid) {
        return;
    }
    Expect(v.major == c.major, label + " major: 期望 " + std::to_string(c.major) + " 实际 " + std::to_string(v.major));
    Expect(v.minor == c.minor, label + " minor: 期望 " + std::to_string(c.minor) + " 实际 " + std::to_string(v.minor));
    Expect(v.revision == c.revision, label + " revision: 期望 " + std::to_string(c.revision) + " 实际 " + std::to_string(v.revision));
    Expect(v.betaversion == c.betaversion, label + " betaversion: 期望 " + std::to_string(c.betaversion) + " 实际 " + std::to_string(v.betaversion));
    Expect(v.status == c.status, label + " status: 期望 " + StatusName(c.status) + " 实际 " + StatusName(v.status));
}

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

int main() {
    for (const auto& c : kCases) {
        RunParseCase(c);
    }

    // 比较运算符: 正式版与测试版序、数值比较、相等语义
    Expect(Version("1.13.1") > Version("1.13.1-beta.1"), "1.13.1 > 1.13.1-beta.1 (正式版大于同号测试版)");
    Expect(Version("1.2") < Version("1.10"), "1.2 < 1.10 (数值比较非字典序)");
    Expect(Version("1.2.3-beta.2") > Version("1.2.3-beta.1"), "1.2.3-beta.2 > 1.2.3-beta.1");
    Expect(Version("1.2.3-beta.1") > Version("1.2.3-alpha.9"), "1.2.3-beta.1 > 1.2.3-alpha.9 (Beta > Dev)");
    Expect(Version("1.2.3") == Version("1.2.3"), "1.2.3 == 1.2.3");
    Expect(Version("1.2.3-beta.1") == Version("1.2.3-beta.1"), "1.2.3-beta.1 == 1.2.3-beta.1");
    Expect(!(Version("1.2.3-beta.1") == Version("1.2.3")), "1.2.3-beta.1 != 1.2.3");
    Expect(Version("1.2") <= Version("1.2"), "1.2 <= 1.2");

    if (g_failed == 0) {
        std::printf("Version 表驱动测试全部通过: %d 项断言\n", g_total);
        return 0;
    }
    std::printf("Version 表驱动测试失败 %d / %d 项断言\n", g_failed, g_total);
    return 1;
}
