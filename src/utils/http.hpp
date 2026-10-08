#ifndef UTILS_HTTP_HPP
#define UTILS_HTTP_HPP

// HTTP 网络：TLS 证书校验配置、数据读取与程序自动更新（断点续传）
// 由 utils.hpp 伞形汇总，调用方统一使用 utils:: 前缀

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdint>
#include <cctype>
#include <windows.h>
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "http/httplib.h"
#include <openssl/evp.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace utils {

    // 配置TLS: 启用服务器证书校验, httplib未指定CA文件时会自动加载Windows系统根证书存储作为信任锚
    inline void SetupTlsVerification(httplib::Client& cli) {
        cli.enable_server_certificate_verification(true);
    }

    // 计算文件的 SHA256 十六进制小写字符串; 读取或计算失败时返回空串
    inline auto ComputeFileSha256(const fs::path& file) -> std::string {
        std::ifstream in(file, std::ios::binary);
        if(!in) return "";
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if(!ctx) return "";
        bool ok = EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1;
        std::vector<char> buf(64 * 1024);
        while(ok && in) {
            in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            const std::streamsize n = in.gcount();
            if(n > 0) {
                ok = EVP_DigestUpdate(ctx, buf.data(), static_cast<size_t>(n)) == 1;
            }
        }
        if(ok && !in.eof()) ok = false; // 非正常结束(读取错误)
        unsigned char md[EVP_MAX_MD_SIZE] = {};
        unsigned int md_len = 0;
        if(ok) ok = EVP_DigestFinal_ex(ctx, md, &md_len) == 1;
        EVP_MD_CTX_free(ctx);
        if(!ok) return "";
        static const char hexchars[] = "0123456789abcdef";
        std::string hex;
        hex.reserve(md_len * 2);
        for(unsigned int i = 0; i < md_len; i++) {
            hex.push_back(hexchars[md[i] >> 4]);
            hex.push_back(hexchars[md[i] & 0xF]);
        }
        return hex;
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

    // ── 自动更新 ──────────────────────────────────────────────
    // DownloadToFile / VerifyDownloadedFile / LaunchReplaceAndRestart 为 UpdateProgram 的内部辅助

    /**
     * @brief 下载新版本到临时文件（支持断点续传）
     *        非 200/206 的响应(如 403/404)会中止请求, 不把错误响应体写入临时文件
     * @param url_host 下载主机（如 https://github.com）
     * @param params 下载路径
     * @param proxy 代理 host:port
     * @param tmp_file 临时文件路径（形如 xxx.exe.new）
     * @param max_size 预期文件大小（>0 时用于判断续传进度）
     * @return 是否成功（临时文件状态可用于后续校验）
     */
    inline auto DownloadToFile(const std::string& url_host, const std::string& params, const std::pair<std::string, int>& proxy, const fs::path& tmp_file, int64_t max_size) -> bool {
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

        if(downloaded_bytes < static_cast<uint64_t>(max_size)) {
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

            int bad_status = 0; // 非 200/206 的状态码(用于错误提示)
            auto res = cli.Get(params, headers,
            [&](const httplib::Response& response) {
                // 服务器忽略 Range 返回 200 时，从头覆盖，避免追加写入导致文件损坏
                if(downloaded_bytes > 0 && response.status == httplib::StatusCode::OK_200) {
                    spdlog::warn("服务器不支持断点续传, 将从头下载");
                    downfile.close();
                    downfile.open(tmp_file, std::ios::binary | std::ios::out | std::ios::trunc);
                    downloaded_bytes = 0;
                }
                // 非 200/206(如 403/404 的错误响应体)时中止请求,
                // 避免错误内容写入 .new 污染断点续传(续传追加后大小虽会凑对但内容已损坏)
                if(response.status != httplib::StatusCode::OK_200 && response.status != httplib::StatusCode::PartialContent_206) {
                    bad_status = response.status;
                    return false;
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
                if(bad_status) {
                    spdlog::error("更新错误, 服务器返回错误的状态码: {} (响应体未写入临时文件)", bad_status);
                }
                else {
                    spdlog::error("网络请求失败:{}", httplib::to_string(res.error()));
                }
                return false;
            }
            if(res->status != httplib::StatusCode::OK_200 && res->status != httplib::StatusCode::PartialContent_206) {
                spdlog::error("更新错误, 服务器返回错误的状态码: {}", res->status);
                return false;
            }
        }
        return true;
    }

    /**
     * @brief 校验临时文件: 文件大小与发布资产声明的 SHA256; 失败时删除临时文件
     * @param tmp_file 临时文件路径
     * @param max_size 预期文件大小（0 表示不校验大小）
     * @param expected_digest 发布资产声明的摘要（形如 "sha256:<hex>"），为空则跳过内容校验
     */
    inline auto VerifyDownloadedFile(const fs::path& tmp_file, int64_t max_size, const std::string& expected_digest) -> bool {
        // 大小校验, 防止不完整的文件被替换进程序目录
        if(max_size > 0 && (!fs::exists(tmp_file) || fs::file_size(tmp_file) != static_cast<uint64_t>(max_size))) {
            spdlog::error("下载文件大小校验失败({} 应为 {}), 已删除临时文件, 请重新运行重试",
                          fs::exists(tmp_file) ? (int64_t)fs::file_size(tmp_file) : (int64_t)-1, max_size);
            std::error_code ec;
            fs::remove(tmp_file, ec);
            return false;
        }

        // 内容摘要校验, 防止被篡改或不完整的资产被替换执行
        if(expected_digest.empty()) {
            spdlog::debug("发布资产未提供SHA256摘要, 跳过完整性校验");
            return true;
        }
        const std::string prefix = "sha256:";
        if(expected_digest.rfind(prefix, 0) != 0) {
            spdlog::warn("发布资产摘要格式无法识别({}), 跳过完整性校验", expected_digest);
            return true;
        }
        const std::string want = expected_digest.substr(prefix.size());
        const std::string got = ComputeFileSha256(tmp_file);
        if(got.empty()) {
            spdlog::warn("计算下载文件摘要失败, 跳过完整性校验");
            return true;
        }
        bool same = want.size() == got.size();
        for(size_t i = 0; same && i < want.size(); i++) {
            same = static_cast<char>(std::tolower(static_cast<unsigned char>(want[i]))) == got[i];
        }
        if(!same) {
            spdlog::error("下载文件 SHA256 校验失败, 已删除临时文件, 请重新运行重试");
            spdlog::error("预期: {} 实际: {}", want, got);
            std::error_code ec;
            fs::remove(tmp_file, ec);
            return false;
        }
        spdlog::info("SHA256 完整性校验通过");
        return true;
    }

    // 拉起 cmd 脚本: 等待当前进程退出后, 用临时文件替换自身并重启
    // 全程宽字符 + 文件名引号转义: 兼容含空格/%/&等特殊字符的文件名
    inline void LaunchReplaceAndRestart(const fs::path& self, const fs::path& tmp_file) {
        auto quote_name = [](std::wstring name) {
            std::wstring out = L"\"";
            for(wchar_t c : name) {
                if(c == L'%') {
                    out += L"%%";   // cmd 中 % 即使位于引号内仍会展开, 需转义
                }
                else {
                    out += c;
                }
            }
            out += L"\"";
            return out;
        };
        const fs::path exe_name = self.filename();
        std::wstring p = L"/c ping 127.0.0.1 -n 6 > nul & move /Y ";
        p += quote_name(tmp_file.filename().wstring());
        p += L" ";
        p += quote_name(exe_name.wstring());
        p += L" & start \"\" ";
        p += quote_name(exe_name.wstring());
        ShellExecuteW(
            NULL,                       // 父窗口句柄
            L"open",                    // 操作
            L"cmd.exe",                 // 应用程序
            p.c_str(),                  // 参数
            self.parent_path().wstring().c_str(), // 工作目录
            SW_SHOW);                   // 显示方式
    }

    /**
     * @brief 自动更新：下载新版本到临时文件（支持断点续传），校验大小与SHA256后拉起替换脚本并退出
     * @param url_host 下载主机（如 https://github.com）
     * @param params 下载路径
     * @param Self 当前可执行文件路径
     * @param max_size 预期文件大小（0 表示不校验）
     * @param proxy 代理 host:port
     * @param expected_digest 发布资产声明的摘要（形如 "sha256:<hex>"），为空则跳过内容校验
     * @return 是否成功（成功后调用方应立即退出等待替换）
     */
    inline auto UpdateProgram(std::string url_host, std::string params, fs::path Self, int64_t max_size, std::pair<std::string, int> proxy, std::string expected_digest = "") -> bool {
        fs::path tmp_file = Self;
        tmp_file += ".new"; // 下载临时文件

        if(!DownloadToFile(url_host, params, proxy, tmp_file, max_size)) {
            return false;
        }
        if(!VerifyDownloadedFile(tmp_file, max_size, expected_digest)) {
            return false;
        }

        spdlog::info("下载完成, 请稍等, 随后自动完成并(无参)重启..");
        LaunchReplaceAndRestart(Self, tmp_file);
        return true;
    }

}

#endif // !UTILS_HTTP_HPP
