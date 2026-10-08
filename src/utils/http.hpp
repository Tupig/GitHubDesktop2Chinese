#ifndef UTILS_HTTP_HPP
#define UTILS_HTTP_HPP

// HTTP 网络：TLS 证书校验配置、数据读取与程序自动更新（断点续传）
// 由 utils.hpp 伞形汇总，调用方统一使用 utils:: 前缀

#include <string>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdint>
#include <windows.h>
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "http/httplib.h"
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace utils {

    // 配置TLS: 启用服务器证书校验, httplib未指定CA文件时会自动加载Windows系统根证书存储作为信任锚
    inline void SetupTlsVerification(httplib::Client& cli) {
        cli.enable_server_certificate_verification(true);
    }

    /**
     * @brief 从网络上读取数据
     * @param url_host 网络主机
     * @param params 请求链接后缀
     * @param out 成功后输出读取到的数据
     * @return 是否成功
     */
    inline auto ReadHttpDataString(std::string url_host, std::string params, std::string& out, std::pair<std::string, int> proxy = {}) -> bool {
        httplib::Client cli(url_host);
        if(proxy.second) {
            cli.set_proxy(proxy.first, proxy.second);
        }
        SetupTlsVerification(cli);
        cli.set_follow_location(true);                          //https://raw.github.com 会要求301重定向
        auto res = cli.Get(params);
        if(!res) {
            for(int i = 0; i < 3; i++) {
                res = cli.Get(params);
                if(res) break;
            }
        }
        if (res) {
            if (res->status == httplib::StatusCode::OK_200) {
                out = res->body;
                return true;
            }
            spdlog::warn("请求 {}{} 返回状态码 {}", url_host, params, res->status);
            return false;
        }
        else {
            spdlog::warn("请求 {}{} 失败: {}", url_host, params, httplib::to_string(res.error()));
            return false;
        }
    }

    /**
     * @brief 自动更新：下载新版本到临时文件（支持断点续传），校验大小后拉起替换脚本并退出
     * @param url_host 下载主机（如 https://github.com）
     * @param params 下载路径
     * @param Self 当前可执行文件路径
     * @param max_size 预期文件大小（0 表示不校验）
     * @param proxy 代理 host:port
     * @return 是否成功（成功后调用方应立即退出等待替换）
     */
    inline auto UpdateProgram(std::string url_host, std::string params, fs::path Self, int64_t max_size, std::pair<std::string, int> proxy) -> bool {
        fs::path parent_dir = Self.parent_path();   // 文件所在目录
        fs::path exe_name = Self.filename();                // 文件名 包含扩展名,但不包含路径

        fs::path tmp_file = Self;
        tmp_file += ".new"; // 下载临时文件

        // 1. 如果临时文件已存在，获取已下载大小（用于断点续传）
        uint64_t downloaded_bytes = 0;
        if(fs::exists(tmp_file)) {
            downloaded_bytes = fs::file_size(tmp_file);
        }

        // 临时文件超过预期大小(异常残留)时丢弃, 重新完整下载
        if(max_size > 0 && downloaded_bytes > static_cast<uint64_t>(max_size)) {
            spdlog::warn("临时文件大小异常, 将重新下载");
            std::error_code ec;
            fs::remove(tmp_file, ec);
            downloaded_bytes = 0;
        }

        if(downloaded_bytes < max_size) {
            // 以 二进制追加模式 打开文件（断点续传关键）
            std::ofstream downfile(tmp_file, std::ios::binary | std::ios::out | std::ios::app);
            if(!downfile.is_open()) {
                spdlog::error("无法打开临时文件写入新版本");
                return false;
            }

            httplib::Client cli(url_host);
            if(proxy.second) {
                cli.set_proxy(proxy.first, proxy.second);
            }
            SetupTlsVerification(cli);
            cli.set_follow_location(true);                          //https://raw.github.com 会要求301重定向
            httplib::Headers headers;
            if(downloaded_bytes > 0) {
                headers.emplace("Range", "bytes=" + std::to_string(downloaded_bytes) + "-");
            }
            headers.emplace("Accept", "application/octet-stream");

            auto res = cli.Get(params, headers,
            [&](const httplib::Response& response) {
                // 服务器忽略 Range 返回 200 时，从头覆盖，避免追加写入导致文件损坏
                if(downloaded_bytes > 0 && response.status == httplib::StatusCode::OK_200) {
                    spdlog::warn("服务器不支持断点续传, 将从头下载");
                    downfile.close();
                    downfile.open(tmp_file, std::ios::binary | std::ios::out | std::ios::trunc);
                    downloaded_bytes = 0;
                }
                return true;
            },
            [&](const char* data, size_t data_length) {
                if(data_length > 0 && downfile.is_open()) {
                    downfile.write(data, data_length);
                    downfile.flush(); // 立即刷入磁盘，不缓存
                }
                return downfile.is_open();
            },
            [&](uint64_t len, uint64_t total) {
                uint64_t total_ = downloaded_bytes + total;  // 文件总大小
                uint64_t now_ = downloaded_bytes + len;
                int percent_ = total_ ? static_cast<int>(now_ * 100 / total_) : 0;

                printf_s("\r %s %d%% ==>  %lld / %lld", (downloaded_bytes > 0) ? "[续传]" : "[下载]", percent_, now_, total_);
                return true;
            });
            printf_s("\n");
            downfile.close();

            if(!res) {
                spdlog::error("网络请求失败:{}", httplib::to_string(res.error()));
                return false;
            }
            if(res->status != httplib::StatusCode::OK_200 && res->status != httplib::StatusCode::PartialContent_206) {
                spdlog::error("更新错误, 服务器返回错误的状态码: {}", res->status);
                return false;
            }
        }

        // 校验下载结果大小, 防止不完整的文件被替换进程序目录
        if(max_size > 0 && (!fs::exists(tmp_file) || fs::file_size(tmp_file) != static_cast<uint64_t>(max_size))) {
            spdlog::error("下载文件大小校验失败({} 应为 {}), 已删除临时文件, 请重新运行重试",
                          fs::exists(tmp_file) ? (int64_t)fs::file_size(tmp_file) : (int64_t)-1, max_size);
            std::error_code ec;
            fs::remove(tmp_file, ec);
            return false;
        }

        spdlog::info("下载完成, 请稍等, 随后自动完成并(无参)重启..");
        // 完成后创建进程
        // 构建参数(全程宽字符, 避免中文目录/文件名下编码错误导致更新替换失败)
        std::wstring p = L"/c \"ping 127.0.0.1 -n 6 > nul & move /Y ";
        p += tmp_file.filename().wstring();
        p += L" ";
        p += exe_name.wstring();
        p += L" & start ";
        p += exe_name.wstring();
        p += L"\"";
        ShellExecuteW(
            NULL,                       // 父窗口句柄
            L"open",                    // 操作
            L"cmd.exe",                 // 应用程序
            p.c_str(),                  // 参数
            parent_dir.wstring().c_str(),// 工作目录
            SW_SHOW);                   // 显示方式

        return true;
    }

}

#endif // !UTILS_HTTP_HPP
