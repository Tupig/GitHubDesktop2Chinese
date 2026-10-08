#ifndef UTILS_FS_IO_HPP
#define UTILS_FS_IO_HPP

// 文件读写：二进制方式整文件读取 / 原子覆盖写入
// 由 utils.hpp 伞形汇总，调用方统一使用 utils:: 前缀

#include <string>
#include <fstream>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace utils {

    inline auto ReadFile(const std::string& filename) -> std::string {
        std::ifstream fin(filename, std::ios::binary);
        //std::stringstream buffer{};
        //buffer << fin.rdbuf();
        //std::string str(buffer.str());
        return std::string(
            std::istreambuf_iterator<char>(fin),
            std::istreambuf_iterator<char>()
        );
        //return str;
    }

    // 原子写入: 先写 xxx.writing 临时文件, 校验流状态后改名覆盖目标。
    // 写入中断(磁盘满/进程被杀)时原文件保持不变, 不会留下半截损坏的源文件。
    // 返回是否成功; 失败时已清理临时文件。
    inline auto WriteFile(const std::string& filename, std::string& txt) -> bool {
        const std::string tmp_filename = filename + ".writing";
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

}

#endif // !UTILS_FS_IO_HPP
