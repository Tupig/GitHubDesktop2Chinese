#ifndef UTILS_FS_IO_HPP
#define UTILS_FS_IO_HPP

// 文件读写：二进制方式整文件读取 / 覆盖写入
// 由 utils.hpp 伞形汇总，调用方统一使用 utils:: 前缀

#include <string>
#include <fstream>
#include <spdlog/spdlog.h>

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

    inline auto WriteFile(const std::string& filename, std::string& txt) -> void {
        std::ofstream override (filename, std::ios::binary);
        override.write(txt.c_str(), txt.size());
        if (!override) {
            spdlog::error("打开并写入目标文件:{} 时失败", filename);
        }
        override.close();
    }

}

#endif // !UTILS_FS_IO_HPP
