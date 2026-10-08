#ifndef UTILS_INPUT_HPP
#define UTILS_INPUT_HPP

// 交互输入：字符串 / 布尔值读取（支持默认值；输入流结束或非法输入时优雅返回）
// 由 utils.hpp 伞形汇总，调用方统一使用 utils:: 前缀

#include <string>
#include <vector>
#include <iostream>
#include <limits>
#include <format>
#include <exception>
#include <spdlog/spdlog.h>

namespace utils {

    inline auto ReadUserInput_bool(std::vector<std::string> input = {"false", "true"}, int defaultval = -1) -> bool {
        if (input.size() != 2) throw std::exception("读取 bool 类型值时 input 数组长度必须为两个");
        if (defaultval > (int)input.size() - 1 || defaultval < -1) throw std::exception(std::format("defaultval 必须能够指向 input数组，或者为 -1, defaultval:{}" , defaultval).c_str());
        while (true)
        {
            // 输出提示
            if(defaultval == -1)
                spdlog::info("请输入一个表示bool的值({}/{})", input[0], input[1]);
            else
                spdlog::info("请输入一个表示bool的值({}/{} 默认 {})", input[0], input[1], input[defaultval]);

            std::string instr;
            //std::cin >> instr;
            std::cin.clear();
            if(std::cin.rdbuf()->in_avail() > 0) std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            if(!std::getline(std::cin, instr)) {
                // 输入流已结束(管道/重定向运行): 有默认值返回默认, 否则返回false, 避免死循环
                return defaultval != -1 ? (defaultval == 1) : false;
            }

            if (instr == input[0]) {
                return false;
            }
            else if (instr == input[1]) {
                return true;
            }
            else {
                if (defaultval == -1) {
                    // 循环
                    continue;
                }
                else if (defaultval == 0){
                    return false;
                }
                else {
                    return true;
                }
            }
        }
    }

}

#endif // !UTILS_INPUT_HPP
