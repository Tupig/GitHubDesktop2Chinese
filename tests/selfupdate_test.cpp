// 自更新残留清理与替换前置检查测试(#182/#183):
//   CleanupSelfUpdateResidue: 无 .old 时快速返回 / 存在 .old 残留时清理 / 相对路径兜底
//   ReplaceSelfAndRestart 前置检查: 临时文件缺失或为空时拒绝替换(fail-closed)且无副作用
// 注: 完整替换+重启路径涉及真实进程操作(CreateProcessW/fork), 不在单测范围,
//     由本地冒烟(smoke-test.ps1)与发布流程覆盖
// 构建: cmake --build . --target selfupdate_test;  运行: ctest
#include "utils/http.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;

void CheckTrue(const std::string& name, bool cond) {
    g_checks++;
    if(!cond) {
        g_failures++;
        std::printf("  [FAIL] %s\n", name.c_str());
    }
}

void WriteBytes(const fs::path& file, const char* content) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << content;
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("ghd2c_selfupdate_" + utils::RandomTempSuffix());
    std::error_code mk_ec;
    fs::create_directories(dir, mk_ec);
    CheckTrue("临时目录创建成功", !mk_ec && fs::exists(dir));

    // T1: 无 .old 残留 → 立即返回, 无副作用(启动高频路径, 不应产生任何文件变化)
    {
        const fs::path fake_exe = dir / "app.exe";
        WriteBytes(fake_exe, "MZ-dummy");
        utils::CleanupSelfUpdateResidue(fake_exe);
        CheckTrue("T1 无 .old 时主文件保持不变", fs::exists(fake_exe));
        fs::path old_file = fake_exe;
        old_file += ".old";
        CheckTrue("T1 无 .old 时不产生 .old", !fs::exists(old_file));
    }

    // T2: 存在 .old 残留(上次更新的旧版本镜像) → 被清理, 主文件不受影响
    {
        const fs::path fake_exe = dir / "app.exe";
        fs::path old_file = fake_exe;
        old_file += ".old";
        WriteBytes(old_file, "old-image");
        utils::CleanupSelfUpdateResidue(fake_exe);
        CheckTrue("T2 .old 残留被清理", !fs::exists(old_file));
        CheckTrue("T2 主文件不受影响", fs::exists(fake_exe));
    }

    // T3: 相对路径形式(裸文件名, 对应 wargv[0] 非绝对路径场景) → fs::absolute 兜底解析正确
    {
        std::error_code cwd_ec;
        const fs::path prev_cwd = fs::current_path(cwd_ec);
        CheckTrue("T3 读取当前目录成功", !cwd_ec);
        fs::current_path(dir, cwd_ec);
        CheckTrue("T3 切换 cwd 成功", !cwd_ec);
        if(!cwd_ec) {
            WriteBytes("relapp.exe.old", "old-image");
            utils::CleanupSelfUpdateResidue(fs::path("relapp.exe"));
            CheckTrue("T3 相对路径下 .old 被清理", !fs::exists("relapp.exe.old"));
            fs::current_path(prev_cwd, cwd_ec); // 切回, 避免 Windows 下删除占用中的 cwd
        }
    }

    // T4: 替换前置检查 — 临时文件缺失 → 拒绝替换且无副作用(不应创建 self/.old)
    {
        const fs::path self = dir / "self_missing.exe";
        fs::path tmp_file = self;
        tmp_file += ".new";
        CheckTrue("T4 .new 缺失时拒绝替换", !utils::ReplaceSelfAndRestart(self, tmp_file));
        CheckTrue("T4 .new 缺失时无 self 产生", !fs::exists(self));
        fs::path old_file = self;
        old_file += ".old";
        CheckTrue("T4 .new 缺失时无 .old 产生", !fs::exists(old_file));
    }

    // T5: 替换前置检查 — 临时文件为空(下载异常残留) → 拒绝替换(fail-closed)
    {
        const fs::path self = dir / "self_empty.exe";
        fs::path tmp_file = self;
        tmp_file += ".new";
        WriteBytes(tmp_file, "");
        CheckTrue("T5 .new 为空时拒绝替换", !utils::ReplaceSelfAndRestart(self, tmp_file));
        fs::path old_file = self;
        old_file += ".old";
        CheckTrue("T5 .new 为空时无 .old 产生", !fs::exists(old_file));
    }

    std::error_code rm_ec;
    fs::remove_all(dir, rm_ec);

    std::printf("selfupdate_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
