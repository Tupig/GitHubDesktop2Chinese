#ifndef UTILS_INPUT_HPP
#define UTILS_INPUT_HPP

// 交互输入：字符串 / 布尔值读取（支持默认值；输入流结束或非法输入时优雅返回）
// 由 utils.hpp 伞形汇总，调用方统一使用 utils:: 前缀

#include <string>
#include <vector>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace utils {

    // 上一次 stdin 操作是否为 >> 提取(缓冲中残留待清扫的换行); 由调用方在 >> 成功后置位。
    // ReadUserInput_bool 仅在该标志置位时清扫残留行: 无 >> 前置的交互(如 PAUSE 后的提示)
    // 不能吞掉用户输入的整行, 否则用户必须输入两次、空行回退默认值
    inline bool stdin_after_extract = false;

    inline auto ReadUserInput_bool(std::vector<std::string> input = {"false", "true"}, int defaultval = -1) -> bool {
        if (input.size() != 2) throw std::runtime_error("读取 bool 类型值时 input 数组长度必须为两个");
        if (defaultval > (int)input.size() - 1 || defaultval < -1) throw std::runtime_error("defaultval 必须能够指向 input数组，或者为 -1, defaultval:" + std::to_string(defaultval));
        while (true)
        {
            // 输出提示
            if(defaultval == -1)
                spdlog::info("请输入一个表示bool的值({}/{})", input[0], input[1]);
            else
                spdlog::info("请输入一个表示bool的值({}/{} 默认 {})", input[0], input[1], input[defaultval]);

            std::string instr;
            std::cin.clear();
            // 调用契约: 若上一次 stdin 操作是 >> 提取(残留换行留在缓冲), 需先清扫残留行。
            // MSVC filebuf 未重写 showmanyc, in_avail() 恒为 0, 不能用它判断残留 —— 由
            // stdin_after_extract 标志精确标记(>> 成功后置位); 标志为假时不得吞掉用户输入
            if(stdin_after_extract) {
                stdin_after_extract = false;
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            }
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
