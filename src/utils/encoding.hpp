#ifndef UTILS_ENCODING_HPP
#define UTILS_ENCODING_HPP

// 编码转换：宽窄字符串互转与路径构造（UTF-8 优先，兼容 ANSI/ACP）
// 由 utils.hpp 伞形汇总，调用方统一使用 utils:: 前缀

#include <cstddef>
#include <cstdint>
#include <string>
#include <filesystem>
#ifdef _WIN32
// windows.h 前定义(未定义时): 排除 WinSock1 等旧接口, 防止独立包含本头的
// 编译单元中 windows.h 预拉入 winsock.h 与后续 winsock2.h 冲突
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace utils {

    // 宽字符串 -> UTF-8 窄字符串（手写编码，#92）
    // std::wstring_convert/std::codecvt_utf8 已被 C++17 弃用（macOS 构建告警），
    // 行为对齐原实现：sizeof(wchar_t)==2 时按 UTF-16 解码（代理对合并为增补平面码点），
    // ==4 时按 UCS-4 直取，随后统一编码为 UTF-8。
    // 差异：原实现对非法输入抛 std::range_error，此处以 U+FFFD 替换（孤立代理/超上限），
    // 任何输入都不抛异常；实际入参均源自本程序内的合法解码，二者不可分辨。
    inline auto to_byte_string(const std::wstring& input) -> std::string
    {
        std::string output;
        output.reserve(input.size());
        const std::size_t count = input.size();
        for(std::size_t i = 0; i < count; ++i) {
            std::uint32_t cp = static_cast<std::uint32_t>(input[i]);
            if constexpr (sizeof(wchar_t) == 2) {
                if(cp >= 0xD800 && cp <= 0xDBFF) {
                    if(i + 1 < count) {
                        const std::uint32_t low = static_cast<std::uint32_t>(input[i + 1]);
                        if(low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                            ++i;
                        } else {
                            cp = 0xFFFDu; // 高代理未配对
                        }
                    } else {
                        cp = 0xFFFDu;     // 串尾孤立高代理
                    }
                } else if(cp >= 0xDC00 && cp <= 0xDFFF) {
                    cp = 0xFFFDu;         // 孤立低代理
                }
            }
            if(cp > 0x10FFFF) {
                cp = 0xFFFDu;             // 超出 Unicode 码点上限（仅 UCS-4 宽度可能出现）
            }
            if(cp <= 0x7F) {
                output.push_back(static_cast<char>(cp));
            } else if(cp <= 0x7FF) {
                output.push_back(static_cast<char>(0xC0u | (cp >> 6)));
                output.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
            } else if(cp <= 0xFFFF) {
                output.push_back(static_cast<char>(0xE0u | (cp >> 12)));
                output.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                output.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
            } else {
                output.push_back(static_cast<char>(0xF0u | (cp >> 18)));
                output.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
                output.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                output.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
            }
        }
        return output;
    }

    // 窄字符串 -> 路径：优先按 UTF-8 解码（控制台输入已设为 UTF-8），非法 UTF-8 时回退为系统 ANSI 代码页
    inline fs::path to_path(const std::string& input) {
        if(input.empty()) return {};
#ifdef _WIN32
        int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0);
        if(wlen > 0) {
            std::wstring w(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), w.data(), wlen);
            return fs::path(std::move(w));
        }
        wlen = MultiByteToWideChar(CP_ACP, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
        if(wlen > 0) {
            std::wstring w(wlen, L'\0');
            MultiByteToWideChar(CP_ACP, 0, input.data(), static_cast<int>(input.size()), w.data(), wlen);
            return fs::path(std::move(w));
        }
        return fs::path(input);
#else
        // POSIX 路径即字节串, 输入来自 UTF-8 控制台, 直接构造
        return fs::path(input);
#endif
    }

}

#endif // !UTILS_ENCODING_HPP
