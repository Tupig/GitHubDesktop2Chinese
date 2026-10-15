#ifndef UTILS_FS_IO_HPP
#define UTILS_FS_IO_HPP

// 文件读写：二进制方式整文件读取 / 原子覆盖写入
// 由 utils.hpp 伞形汇总，调用方统一使用 utils:: 前缀

#include <string>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <cerrno>
#include <random>
#include <spdlog/spdlog.h>
#ifdef _WIN32
// windows.h 前定义(未定义时): 排除 WinSock1 等旧接口, 防止独立包含本头的
// 编译单元中 windows.h 预拉入 winsock.h 与后续 winsock2.h 冲突
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace utils {

    // 临时文件随机后缀: 8 位十六进制随机串, 降低固定可预测临时文件名
    // 被抢注/符号链接预置的攻击面(#122 B2)
    inline auto RandomTempSuffix() -> std::string {
        char buf[16];
        std::snprintf(buf, sizeof(buf), ".%08x", std::random_device{}());
        return std::string(buf);
    }

    inline auto ReadFile(const std::string& filename) -> std::string {
        std::ifstream fin(filename, std::ios::binary);
        return std::string(
            std::istreambuf_iterator<char>(fin),
            std::istreambuf_iterator<char>()
        );
    }

    // rename 前强制数据落盘(#171): flush/close/copy 完成时数据仅驻 OS 缓存,
    // 日志文件系统(NTFS 等)可使 rename 元数据先于数据持久化, 断电后目标为半截/空文件。
    // 失败返回 false 并将原因记入 ec; 落盘未确认的文件不允许进入 rename。
    inline auto FlushToDisk(const fs::path& file, std::error_code& ec) -> bool {
#ifdef _WIN32
        // 共享模式放开(#244): FlushFileBuffers 只需写权限; 独占打开(0)在杀软/索引器
        // 短暂持有句柄时会误判"落盘失败", 进而使 WriteFile/AtomicCopyFile 无谓失败
        HANDLE h = CreateFileW(file.wstring().c_str(), GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if(h == INVALID_HANDLE_VALUE) {
            ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
            return false;
        }
        const BOOL flushed = FlushFileBuffers(h);
        const DWORD err = GetLastError();
        CloseHandle(h);
        if(!flushed) {
            ec = std::error_code(static_cast<int>(err), std::system_category());
            return false;
        }
#else
        const int fd = ::open(file.c_str(), O_WRONLY);
        if(fd < 0) {
            ec = std::error_code(errno, std::system_category());
            return false;
        }
        const int synced = ::fsync(fd);
        const int err = errno;
        ::close(fd);
        if(synced != 0) {
            ec = std::error_code(err, std::system_category());
            return false;
        }
#endif
        ec.clear();
        return true;
    }

    // 原子写入: 先写 xxx.writing 临时文件, 校验流状态后改名覆盖目标。
    // 写入中断(磁盘满/进程被杀)时原文件保持不变, 不会留下半截损坏的源文件。
    // 返回是否成功; 失败时已清理临时文件。
    inline auto WriteFile(const std::string& filename, const std::string& txt) -> bool {
        // 临时文件名附加随机后缀: 防止固定名称被抢注/符号链接预置(#122 B2)
        const std::string tmp_filename = filename + ".writing" + RandomTempSuffix();
        {
            std::ofstream out(tmp_filename, std::ios::binary | std::ios::trunc);
            if(!out.is_open()) {
                spdlog::error("无法打开临时文件写入: {}", tmp_filename);
                return false;
            }
            out.write(txt.c_str(), static_cast<std::streamsize>(txt.size()));
            out.flush();
            out.close();
            if(!out) {
                spdlog::error("写入临时文件失败: {}", tmp_filename);
                std::error_code ec;
                fs::remove(tmp_filename, ec);
                return false;
            }
        }
        // rename 前数据落盘(#171): 数据仅驻 OS 缓存时 NTFS 元数据日志可让 rename 先持久化
        std::error_code sync_ec;
        if(!FlushToDisk(fs::path(tmp_filename), sync_ec)) {
            spdlog::error("临时文件落盘失败: {}: {}", tmp_filename, sync_ec.message());
            std::error_code ec;
            fs::remove(tmp_filename, ec);
            return false;
        }
        std::error_code ec;
        fs::rename(tmp_filename, filename, ec);
        if(ec) {
            spdlog::error("替换目标文件失败 {} -> {}: {}", tmp_filename, filename, ec.message());
            std::error_code rmec;
            fs::remove(tmp_filename, rmec);
            return false;
        }
        return true;
    }

    // 原子复制: 先复制到 "dst.writing + 随机后缀" 临时文件, 成功后改名覆盖目标(#166)。
    // 与 WriteFile 同一原子范式; 复制中断(磁盘满/进程被杀)只留临时文件或由本函数清理,
    // 不会留下半截目标文件 —— 修复 fs::copy_file 直写目标时中断产生半截文件、
    // 且目标 fs::exists 判定为真导致备份/恢复自愈机制失效的缺陷。
    // src 不存在时 ec 为 no_such_file_or_directory; 失败时已清理临时文件。
    inline auto AtomicCopyFile(const fs::path& src, const fs::path& dst, std::error_code& ec) -> bool {
        const fs::path tmp = dst.string() + ".writing" + RandomTempSuffix();
        fs::copy_file(src, tmp, fs::copy_options::overwrite_existing, ec);
        if(ec) {
            std::error_code rmec;
            fs::remove(tmp, rmec); // copy 中断可能留半截临时文件; 无临时文件时忽略
            return false;
        }
        // rename 前数据落盘(#171): 与 WriteFile 同一原子范式, copy 完成时数据同样仅驻 OS 缓存
        if(!FlushToDisk(tmp, ec)) {
            std::error_code rmec;
            fs::remove(tmp, rmec);
            return false;
        }
        fs::rename(tmp, dst, ec);
        if(ec) {
            std::error_code rmec;
            fs::remove(tmp, rmec);
            return false;
        }
        return true;
    }

}

#endif // !UTILS_FS_IO_HPP
