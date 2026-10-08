#ifndef UTILS_SYSTEM_HPP
#define UTILS_SYSTEM_HPP

// 系统信息：环境变量读取与系统代理探测（环境变量优先，其次 IE/WinHTTP 配置）
// 由 utils.hpp 伞形汇总，调用方统一使用 utils:: 前缀

#include <string>
#include <optional>
#include <utility>
#include <iostream>
#include <windows.h>
#include <winhttp.h>

namespace utils {

    inline std::string GetEnvVar(const std::string& varName) {
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
    }

    inline std::optional<std::pair<std::string, int>> get_proxy_env() {
        auto p1 = GetEnvVar("HTTPS_PROXY");
        if(p1.empty()) p1= GetEnvVar("https_proxy");

        if(p1.empty()) {
            return {};
        }
        size_t protocolEnd = p1.find("://");
        std::string addrWithoutProtocol = (protocolEnd != std::string::npos)
            ? p1.substr(protocolEnd + 3)
            : p1;

        // 2. 分割主机和端口（以第一个 : 为界）
        size_t colonPos = addrWithoutProtocol.find(':');
        if(colonPos == std::string::npos) {
            std::cerr << "错误：代理地址无端口号！" << std::endl;
            return {};
        }
        // 3. 提取主机和端口
        int port = 0;
        std::string host = addrWithoutProtocol.substr(0, colonPos);
        try {
            port = std::stoi(addrWithoutProtocol.substr(colonPos + 1));
        }
        catch(const std::exception& e) {
            std::cerr << "错误：端口号格式无效 - " << e.what() << std::endl;
            return {};
        }
        if(port) {
            return std::make_pair(host, port);
        }
        return {};
    }

    inline std::optional<std::pair<std::string, int>> GetSystemProxySettings() {
        std::string address;
        int port = 0;
        // Windows 实现
        WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ieProxyConfig = { 0 };

        if(WinHttpGetIEProxyConfigForCurrentUser(&ieProxyConfig)) {
            if(ieProxyConfig.lpszProxy) {
                //config.enabled = true;
                std::wstring proxyW(ieProxyConfig.lpszProxy);
                address = { proxyW.begin(), proxyW.end() };

                // IE 代理配置可能为 "http=host:port;https=host:port" 形式：
                // 取第一段并去除 scheme= 前缀
                size_t semiPos = address.find(';');
                if(semiPos != std::string::npos) {
                    address = address.substr(0, semiPos);
                }
                size_t eqPos = address.find('=');
                if(eqPos != std::string::npos) {
                    address = address.substr(eqPos + 1);
                }

                // 尝试解析端口 (格式: address:port)
                size_t pos = address.find(':');
                if(pos != std::string::npos) {
                    try {
                        port = std::stoi(address.substr(pos + 1));
                        address = address.substr(0, pos);
                    }
                    catch(...) {
                        // 端口解析失败
                    }
                }
            }

            // 清理资源
            if(ieProxyConfig.lpszProxy) GlobalFree(ieProxyConfig.lpszProxy);
            if(ieProxyConfig.lpszProxyBypass) GlobalFree(ieProxyConfig.lpszProxyBypass);
            if(ieProxyConfig.lpszAutoConfigUrl) GlobalFree(ieProxyConfig.lpszAutoConfigUrl);
        }
        if(port) {
            return std::make_pair(address, port);
        }
        return {};
    }

}

#endif // !UTILS_SYSTEM_HPP
