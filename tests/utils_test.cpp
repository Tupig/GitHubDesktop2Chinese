// utils 头文件纯逻辑单元测试(#263): 编码转换 / 路径构造 / 随机后缀 / 原子文件写入
// 测试框架: GoogleTest(third_party/googletest)
// 构建: cmake --build . --target utils_test;  运行: ctest
#include "utils/encoding.hpp"
#include "utils/fs_io.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <regex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// 生成唯一临时目录, 测试结束自行清理
class TempDir {
public:
    TempDir() {
        path_ = fs::temp_directory_path() / ("ghd2c_utils_" + utils::RandomTempSuffix().substr(1));
        std::error_code ec;
        fs::create_directories(path_, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

std::vector<fs::path> leftover_temp_files(const fs::path& dir) {
    std::vector<fs::path> out;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().filename().string().find(".writing") != std::string::npos) {
            out.push_back(entry.path());
        }
    }
    return out;
}

} // namespace

// ── to_byte_string: 宽字符 -> UTF-8 ──
TEST(Encoding, AsciiPassthrough) {
    EXPECT_EQ(utils::to_byte_string(L"abc"), "abc");
}

TEST(Encoding, EmptyInput) {
    EXPECT_TRUE(utils::to_byte_string(L"").empty());
}

TEST(Encoding, TwoByteLatin) {
    EXPECT_EQ(utils::to_byte_string(L"\u00e9"), "\xC3\xA9");
}

TEST(Encoding, ThreeByteCjk) {
    EXPECT_EQ(utils::to_byte_string(L"中文"), "\xE4\xB8\xAD\xE6\x96\x87");
}

TEST(Encoding, ThreeByteCurrencySign) {
    EXPECT_EQ(utils::to_byte_string(L"\u20ac"), "\xE2\x82\xAC");
}

#if defined(_WIN32)
// wchar_t 为 2 字节时走 UTF-16 解码路径（POSIX 的 4 字节 wchar_t 为 UCS-4 直取, 语义不同）
TEST(Encoding, SurrogatePairDecodesToFourBytes) {
    EXPECT_EQ(utils::to_byte_string(L"\xD83D\xDE00"), "\xF0\x9F\x98\x80");
}

TEST(Encoding, UnpairedHighSurrogateReplaced) {
    const std::wstring s(1, static_cast<wchar_t>(0xD83D));
    EXPECT_EQ(utils::to_byte_string(s), "\xEF\xBF\xBD");
}

TEST(Encoding, IsolatedLowSurrogateReplaced) {
    const std::wstring s(1, static_cast<wchar_t>(0xDC00));
    EXPECT_EQ(utils::to_byte_string(s), "\xEF\xBF\xBD");
}

TEST(Encoding, HighSurrogateFollowedByNonLowReplaced) {
    const std::wstring s = std::wstring(1, static_cast<wchar_t>(0xD83D)) + L"A";
    EXPECT_EQ(utils::to_byte_string(s), "\xEF\xBF\xBD" "A");
}
#else
// wchar_t 为 4 字节(UCS-4 直取)时才可能出现超出 Unicode 上限的码点;
// 该分支在 Linux CI 上执行, Windows(wchar_t=2) 下由 UTF-16 解码路径覆盖。
TEST(Encoding, BeyondUnicodeMaxReplaced) {
    const std::wstring s(1, static_cast<wchar_t>(0x110000));
    EXPECT_EQ(utils::to_byte_string(s), "\xEF\xBF\xBD");
}
#endif

// ── to_path: 窄字符串 -> 路径 ──
TEST(ToPath, EmptyYieldsEmptyPath) {
    EXPECT_TRUE(utils::to_path("").empty());
}

TEST(ToPath, AsciiPathPreserved) {
    EXPECT_EQ(utils::to_path("dir/file.txt").filename().string(), "file.txt");
}

TEST(ToPath, Utf8PathRoundTripsWithToByteString) {
    const fs::path p = utils::to_path("目录/文件.txt");
    EXPECT_EQ(utils::to_byte_string(p.filename().wstring()), "文件.txt");
}

// ── RandomTempSuffix ──
TEST(RandomTempSuffix, FormatIsDotPlusEightHexDigits) {
    EXPECT_TRUE(std::regex_match(utils::RandomTempSuffix(), std::regex("^\\.[0-9a-f]{8}$")));
}

TEST(RandomTempSuffix, NonEmptyAcrossCalls) {
    EXPECT_FALSE(utils::RandomTempSuffix().empty());
    EXPECT_FALSE(utils::RandomTempSuffix().empty());
}

// ── ReadFile / WriteFile ──
TEST(FsIo, WriteThenReadRoundTripPreservesUtf8) {
    TempDir dir;
    const std::string target = (dir.path() / "out.txt").string();
    const std::string content = "中文内容 with ascii\n第二行";
    ASSERT_TRUE(utils::WriteFile(target, content));
    EXPECT_EQ(utils::ReadFile(target), content);
    EXPECT_TRUE(leftover_temp_files(dir.path()).empty()) << "原子写入不应残留 .writing 临时文件";
}

TEST(FsIo, WriteAtomicallyOverwritesExistingFile) {
    TempDir dir;
    const std::string target = (dir.path() / "out.txt").string();
    ASSERT_TRUE(utils::WriteFile(target, "old"));
    ASSERT_TRUE(utils::WriteFile(target, "new-content"));
    EXPECT_EQ(utils::ReadFile(target), "new-content");
    EXPECT_TRUE(leftover_temp_files(dir.path()).empty());
}

TEST(FsIo, WriteToMissingDirectoryFails) {
    TempDir dir;
    const std::string target = (dir.path() / "no_such_subdir" / "out.txt").string();
    EXPECT_FALSE(utils::WriteFile(target, "x"));
}

TEST(FsIo, ReadMissingFileYieldsEmpty) {
    TempDir dir;
    EXPECT_TRUE(utils::ReadFile((dir.path() / "missing.txt").string()).empty());
}

// ── FlushToDisk ──
TEST(FsIo, FlushToDiskOnExistingFileSucceeds) {
    TempDir dir;
    const std::string target = (dir.path() / "data.bin").string();
    ASSERT_TRUE(utils::WriteFile(target, "payload"));
    std::error_code ec;
    EXPECT_TRUE(utils::FlushToDisk(fs::path(target), ec));
    EXPECT_FALSE(ec);
}

TEST(FsIo, FlushToDiskOnMissingFileFailsWithError) {
    TempDir dir;
    std::error_code ec;
    EXPECT_FALSE(utils::FlushToDisk(dir.path() / "missing.bin", ec));
    EXPECT_TRUE(static_cast<bool>(ec));
}

// ── AtomicCopyFile ──
TEST(FsIo, AtomicCopyFileCopiesContent) {
    TempDir dir;
    const fs::path src = dir.path() / "src.txt";
    ASSERT_TRUE(utils::WriteFile(src.string(), "copy-me"));
    const fs::path dst = dir.path() / "dst.txt";
    std::error_code ec;
    ASSERT_TRUE(utils::AtomicCopyFile(src, dst, ec));
    EXPECT_FALSE(ec);
    EXPECT_EQ(utils::ReadFile(dst.string()), "copy-me");
    EXPECT_TRUE(leftover_temp_files(dir.path()).empty());
}

TEST(FsIo, AtomicCopyFileOverwritesExistingTarget) {
    TempDir dir;
    const fs::path src = dir.path() / "src.txt";
    const fs::path dst = dir.path() / "dst.txt";
    ASSERT_TRUE(utils::WriteFile(src.string(), "new"));
    ASSERT_TRUE(utils::WriteFile(dst.string(), "old"));
    std::error_code ec;
    ASSERT_TRUE(utils::AtomicCopyFile(src, dst, ec));
    EXPECT_EQ(utils::ReadFile(dst.string()), "new");
}

TEST(FsIo, AtomicCopyFileMissingSourceFails) {
    TempDir dir;
    const fs::path dst = dir.path() / "dst.txt";
    std::error_code ec;
    EXPECT_FALSE(utils::AtomicCopyFile(dir.path() / "missing.txt", dst, ec));
    EXPECT_TRUE(static_cast<bool>(ec));
    EXPECT_FALSE(fs::exists(dst));
}
