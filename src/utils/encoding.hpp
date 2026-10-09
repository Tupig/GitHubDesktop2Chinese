#ifndef UTILS_ENCODING_HPP
#define UTILS_ENCODING_HPP

// 编码转换：宽窄字符串互转与路径构造（UTF-8 优先，兼容 ANSI/ACP）
// 由 utils.hpp 伞形汇总，调用方统一使用 utils:: 前缀

#include <string>
#include <codecvt>
#include <filesystem>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace utils {

    inline auto utf8ToAnsi(const std::string& utf8String) -> std::string {
#ifdef _WIN32
        int utf8Size = static_cast<int>(utf8String.size());
        int ansiSize = MultiByteToWideChar(CP_UTF8, 0, utf8String.c_str(), utf8Size, nullptr, 0);
        std::vector<wchar_t> wideString(ansiSize);
        MultiByteToWideChar(CP_UTF8, 0, utf8String.c_str(), utf8Size, wideString.data(), ansiSize);
        ansiSize = WideCharToMultiByte(CP_ACP, 0, wideString.data(), ansiSize, nullptr, 0, nullptr, nullptr);
        std::vector<char> ansiString(ansiSize);
        WideCharToMultiByte(CP_ACP, 0, wideString.data(), ansiSize, ansiString.data(), ansiSize, nullptr, nullptr);
        return std::string(ansiString.begin(), ansiString.end());
#else
        // POSIX 终端使用 UTF-8, 无 ANSI 代码页概念, 直接透传
        return utf8String;
#endif
    }

    inline auto to_byte_string(const std::wstring& input) -> std::string
    {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.to_bytes(input);
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
