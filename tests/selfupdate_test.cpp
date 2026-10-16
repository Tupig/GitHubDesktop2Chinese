// 自更新残留清理与替换前置检查测试(#182/#183):
//   CleanupSelfUpdateResidue: 无 .old 时快速返回 / 存在 .old 残留时清理 / 相对路径兜底
//   ReplaceSelfAndRestart 前置检查: 临时文件缺失或为空时拒绝替换(fail-closed)且无副作用
// 注: 完整替换+重启路径涉及真实进程操作(CreateProcessW/fork), 不在单测范围,
//     由本地冒烟(smoke-test.ps1)与发布流程覆盖
// 测试框架: GoogleTest(third_party/googletest)
// 构建: cmake --build . --target selfupdate_test;  运行: ctest
#include "utils/http.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

class SelfUpdateTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / ("ghd2c_selfupdate_" + utils::RandomTempSuffix());
        std::error_code ec;
        fs::create_directories(dir_, ec);
        ASSERT_FALSE(ec) << "临时目录创建失败: " << dir_.string();
        ASSERT_TRUE(fs::exists(dir_));
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    static void WriteBytes(const fs::path& file, const char* content) {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << content;
    }

    fs::path dir_;
};

} // namespace

// T1: 无 .old 残留 → 立即返回, 无副作用(启动高频路径, 不应产生任何文件变化)
TEST_F(SelfUpdateTest, NoResidueLeavesFilesUntouched) {
    const fs::path fake_exe = dir_ / "app.exe";
    WriteBytes(fake_exe, "MZ-dummy");
    utils::CleanupSelfUpdateResidue(fake_exe);
    EXPECT_TRUE(fs::exists(fake_exe)) << "T1 无 .old 时主文件应保持不变";
    fs::path old_file = fake_exe;
    old_file += ".old";
    EXPECT_FALSE(fs::exists(old_file)) << "T1 无 .old 时不应产生 .old";
}

// T2: 存在 .old 残留(上次更新的旧版本镜像) → Windows 清理之;
//     POSIX 清理为空操作(rename 原子替换无 .old 机制), 不应删改任何文件
TEST_F(SelfUpdateTest, CleansOldResidueOnWindowsOnly) {
    const fs::path fake_exe = dir_ / "app.exe";
    WriteBytes(fake_exe, "MZ-dummy");
    fs::path old_file = fake_exe;
    old_file += ".old";
    WriteBytes(old_file, "old-image");
    utils::CleanupSelfUpdateResidue(fake_exe);
#ifdef _WIN32
    EXPECT_FALSE(fs::exists(old_file)) << "T2 .old 残留应被清理";
#else
    EXPECT_TRUE(fs::exists(old_file)) << "T2 POSIX 空操作不删 .old";
#endif
    EXPECT_TRUE(fs::exists(fake_exe)) << "T2 主文件应不受影响";
}

// T3: 相对路径形式(裸文件名, 对应 wargv[0] 非绝对路径场景) → fs::absolute 兜底解析正确
TEST_F(SelfUpdateTest, RelativePathResolvedViaAbsolute) {
    std::error_code cwd_ec;
    const fs::path prev_cwd = fs::current_path(cwd_ec);
    EXPECT_FALSE(cwd_ec) << "T3 读取当前目录失败";
    fs::current_path(dir_, cwd_ec);
    EXPECT_FALSE(cwd_ec) << "T3 切换 cwd 失败";
    if(!cwd_ec) {
        WriteBytes("relapp.exe.old", "old-image");
        utils::CleanupSelfUpdateResidue(fs::path("relapp.exe"));
#ifdef _WIN32
        EXPECT_FALSE(fs::exists("relapp.exe.old")) << "T3 相对路径下 .old 应被清理";
#else
        EXPECT_TRUE(fs::exists("relapp.exe.old")) << "T3 POSIX 空操作不崩溃且不动文件";
#endif
        std::error_code restore_ec;
        fs::current_path(prev_cwd, restore_ec); // 切回, 避免 Windows 下删除占用中的 cwd
        EXPECT_FALSE(restore_ec) << "T3 切回原 cwd 失败";
    }
}

// T4: 替换前置检查 — 临时文件缺失 → 拒绝替换且无副作用(不应创建 self/.old)
TEST_F(SelfUpdateTest, RejectsWhenTmpMissing) {
    const fs::path self = dir_ / "self_missing.exe";
    fs::path tmp_file = self;
    tmp_file += ".new";
    EXPECT_FALSE(utils::ReplaceSelfAndRestart(self, tmp_file)) << "T4 .new 缺失时应拒绝替换";
    EXPECT_FALSE(fs::exists(self)) << "T4 .new 缺失时不应产生 self";
    fs::path old_file = self;
    old_file += ".old";
    EXPECT_FALSE(fs::exists(old_file)) << "T4 .new 缺失时不应产生 .old";
}

// T5: 替换前置检查 — 临时文件为空(下载异常残留) → 拒绝替换(fail-closed)
TEST_F(SelfUpdateTest, RejectsWhenTmpEmpty) {
    const fs::path self = dir_ / "self_empty.exe";
    fs::path tmp_file = self;
    tmp_file += ".new";
    WriteBytes(tmp_file, "");
    EXPECT_FALSE(utils::ReplaceSelfAndRestart(self, tmp_file)) << "T5 .new 为空时应拒绝替换";
    fs::path old_file = self;
    old_file += ".old";
    EXPECT_FALSE(fs::exists(old_file)) << "T5 .new 为空时不应产生 .old";
}
