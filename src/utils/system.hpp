#ifndef UTILS_SYSTEM_HPP
#define UTILS_SYSTEM_HPP

// 系统信息：环境变量读取与系统代理探测（环境变量优先，其次 IE/WinHTTP 配置）
// 由 utils.hpp 伞形汇总，调用方统一使用 utils:: 前缀

#include <string>
#include <optional>
#include <utility>
#include <algorithm>
#include <cstdlib>
#include <spdlog/spdlog.h>
#include "utils/encoding.hpp"
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace utils {

    inline std::string GetEnvVar(const std::string& varName) {
#ifdef _WIN32
        // Windows API 优先使用宽字符版本，避免编码问题
        std::wstring wVarName(varName.begin(), varName.end());
        wchar_t* wValue = _wgetenv(wVarName.c_str());

        if(wValue == nullptr) return "";

        // 转换 UTF-16 到 UTF-8
        int len = WideCharToMultiByte(CP_UTF8, 0, wValue, -1, nullptr, 0, nullptr, nullptr);
        std::string value(len, 0);
        WideCharToMultiByte(CP_UTF8, 0, wValue, -1, &value[0], len, nullptr, nullptr);
        value.pop_back(); // 移除末尾的空字符
        return value;
#else
        // POSIX 环境变量为字节串(UTF-8), 直接读取
        const char* value = std::getenv(varName.c_str());
        return value == nullptr ? std::string() : std::string(value);
#endif
    }

    // 解析代理地址: 支持 [http(s)://][user:pass@]host:port 与 [IPv6]:port 形式;
    // userinfo(代理认证)调用侧暂不支持, 解析时忽略但保证 host 正确;
    // 解析失败/端口非法返回空 optional
    inline std::optional<std::pair<std::string, int>> ParseProxyAddress(std::string raw) {
        const size_t schemePos = raw.find("://");
        if(schemePos != std::string::npos) {
            raw = raw.substr(schemePos + 3);
        }
        const size_t atPos = raw.rfind('@'); // 取最后一个 '@': 密码可能含 '@'
        if(atPos != std::string::npos) {
            raw = raw.substr(atPos + 1);
        }
        const size_t slashPos = raw.find('/');
        if(slashPos != std::string::npos) {
            raw = raw.substr(0, slashPos);
        }
        // 未加方括号的裸 IPv6(含两个及以上 ':')无法与 host:port 区分, 直接判无效,
        // 强制使用 [addr]:port 形式(否则 "::1" 会被误解析为 host:"::" port:1)
        if(!raw.empty() && raw.front() != '[' && std::count(raw.begin(), raw.end(), ':') >= 2) {
            return {};
        }
        std::string host;
        std::string portStr;
        if(!raw.empty() && raw.front() == '[') {
            const size_t close = raw.find(']');
            if(close == std::string::npos) return {};
            host = raw.substr(1, close - 1);
            if(close + 1 < raw.size() && raw[close + 1] == ':') {
                portStr = raw.substr(close + 2);
            }
        }
        else {
            const size_t colonPos = raw.rfind(':'); // IPv6 走上方方括号分支, 此处为 host:port
            if(colonPos == std::string::npos) return {};
            host = raw.substr(0, colonPos);
            portStr = raw.substr(colonPos + 1);
        }
        if(host.empty() || portStr.empty()) return {};
        int port = 0;
        try {
            port = std::stoi(portStr);
        }
        catch(const std::exception&) {
            return {};
        }
        if(port <= 0 || port > 65535) return {};
        return std::make_pair(host, port);
    }

    inline std::optional<std::pair<std::string, int>> get_proxy_env() {
        std::string p1 = GetEnvVar("HTTPS_PROXY");
        if(p1.empty()) p1 = GetEnvVar("https_proxy");
        if(p1.empty()) {
            return {};
        }
        auto parsed = ParseProxyAddress(p1);
        if(!parsed) {
            spdlog::warn("错误：代理地址无法解析（需形如 [http://]host:port、[IPv6]:port 或 user:pass@host:port）: {}", p1);
            return {};
        }
        return parsed;
    }

    inline std::optional<std::pair<std::string, int>> GetSystemProxySettings() {
#ifdef _WIN32
        std::optional<std::pair<std::string, int>> result;
        // Windows 实现
        WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ieProxyConfig = { 0 };

        if(WinHttpGetIEProxyConfigForCurrentUser(&ieProxyConfig)) {
            if(ieProxyConfig.lpszProxy) {
                // IE 代理配置可能为 "http=host:port;https=host:port" 形式: 本程序全部请求均为 HTTPS,
                // 优先取 https= 段, 其次 http= 段; 无 scheme 前缀(单地址)时取第一段并剥离可能的 "xxx=" 前缀。
                // 宽转 UTF-8 统一走编码工具, 避免逐字符窄化丢失非 ASCII 主机
                const std::string address = utils::to_byte_string(ieProxyConfig.lpszProxy);
                std::string https_addr, http_addr, first_addr;
                size_t pos = 0;
                while(pos < address.size()) {
                    const size_t semi = address.find(';', pos);
                    const std::string seg = address.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
                    if(!seg.empty() && first_addr.empty()) {
                        first_addr = seg;
                    }
                    const size_t eq = seg.find('=');
                    if(eq != std::string::npos) {
                        const std::string scheme = seg.substr(0, eq);
                        if(scheme == "https" && https_addr.empty()) {
                            https_addr = seg.substr(eq + 1);
                        }
                        else if(scheme == "http" && http_addr.empty()) {
                            http_addr = seg.substr(eq + 1);
                        }
                    }
                    if(semi == std::string::npos) {
                        break;
                    }
                    pos = semi + 1;
                }
                std::string chosen;
                if(!https_addr.empty()) {
                    chosen = https_addr;
                }
                else if(!http_addr.empty()) {
                    chosen = http_addr;
                }
                else if(!first_addr.empty()) {
                    const size_t eq = first_addr.find('=');
                    chosen = eq != std::string::npos ? first_addr.substr(eq + 1) : first_addr;
                }
                result = ParseProxyAddress(chosen);
            }

            // 清理资源
            if(ieProxyConfig.lpszProxy) GlobalFree(ieProxyConfig.lpszProxy);
            if(ieProxyConfig.lpszProxyBypass) GlobalFree(ieProxyConfig.lpszProxyBypass);
            if(ieProxyConfig.lpszAutoConfigUrl) GlobalFree(ieProxyConfig.lpszAutoConfigUrl);
        }
        return result;
#else
        // POSIX 无 IE 代理配置, 统一走环境变量探测
        return get_proxy_env();
#endif
    }

}

#endif // !UTILS_SYSTEM_HPP
