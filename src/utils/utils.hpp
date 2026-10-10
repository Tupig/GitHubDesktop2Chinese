#ifndef UTILS_UTILS_HPP
#define UTILS_UTILS_HPP

// 通用工具伞形汇总头（对外唯一入口）：调用方统一 #include "utils/utils.hpp"
//
// 职责拆分（2026-10, issue #4）：
//   encoding.hpp  编码转换（宽窄字符串/路径构造）
//   system.hpp    系统信息（环境变量/系统代理探测）
//   fs_io.hpp     文件读写（整文件二进制读写）
//   http.hpp      HTTP（TLS 校验/数据读取/自动更新）
//   input.hpp     交互输入（字符串/布尔值读取）
//
// 所有工具函数位于 namespace utils，新增职责请放入对应子头并在此登记。

#include <string>
#include <filesystem>

#include "utils/encoding.hpp"
#include "utils/system.hpp"
#include "utils/fs_io.hpp"
#include "utils/http.hpp"
#include "utils/input.hpp"

namespace fs = std::filesystem;

#endif // !UTILS_UTILS_HPP
