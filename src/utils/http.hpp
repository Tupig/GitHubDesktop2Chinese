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
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <thread>
#include <chrono>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h> // ShellExecuteW(LaunchReplaceAndRestart): 显式包含, 不依赖 windows.h 恰好间接拉入
#else
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#endif
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "http/httplib.h"
#include <openssl/evp.h>
#include <spdlog/spdlog.h>
#include "utils/fs_io.hpp"

namespace fs = std::filesystem;

namespace utils {

    // 网络超时(秒): 防止失效代理指向黑洞地址时按 httplib 默认 300s × 4 次重试卡死约 20 分钟
    inline constexpr int kConnectTimeoutSec = 10;
    inline constexpr int kReadTimeoutSec = 30;
    // 单次请求可接受的响应体上限(32MB): 防止异常/恶意服务端响应撑爆内存
    inline constexpr size_t kMaxResponseBytes = 32 * 1024 * 1024;
    // 哈希计算时文件分块读取的缓冲区大小
    inline constexpr size_t kHashBufferSize = 64 * 1024;
    // 瞬时性错误(连接失败/403/429/5xx)自动重试次数上限
    inline constexpr int kHttpRetryLimit = 4;
    // 瞬时性错误重试间隔(秒); 服务端 Retry-After 更大时以其为准
    inline constexpr int kHttpRetryDelaySec = 2;
    // 遵守服务端 Retry-After 的等待上限(秒): 防服务端异常值把交互卡死数分钟(#132)
    inline constexpr int kRetryAfterCapSec = 90;
    // 下载失败重试次数上限
    inline constexpr int kDownloadRetryLimit = 3;
    // 下载失败重试间隔(秒)
    inline constexpr int kDownloadRetryDelaySec = 5;
    // 拉起替换脚本后等待当前进程退出的秒数(ping -n N ≈ N-1 秒 / sleep N)
    inline constexpr int kReplaceWaitSec = 5;

    // 构造带超时与 TLS 校验的 HTTP 客户端
    inline auto MakeHttpClient(const std::string& url_host, const std::pair<std::string, int>& proxy = {}) -> httplib::Client {
        httplib::Client cli(url_host);
        cli.set_connection_timeout(kConnectTimeoutSec);
        cli.set_read_timeout(kReadTimeoutSec);
        if(proxy.second) {
            cli.set_proxy(proxy.first, proxy.second);
        }
        cli.enable_server_certificate_verification(true);
#ifdef _WIN32
        // Windows: httplib 自动加载系统 ROOT 证书存储
#elif defined(__APPLE__)
        // macOS: 使用系统内置 CA 包; 静态 OpenSSL 的编译期默认信任路径指向构建前缀(用户机器上不存在), 必须显式指定
        if(fs::exists("/etc/ssl/cert.pem")) {
            cli.set_ca_cert_path("/etc/ssl/cert.pem");
        }
#else
        // Linux: 按发行版常见位置探测 CA 包(Debian/Ubuntu、RHEL/Fedora、Alpine 等)
        static const char* ca_bundles[] = {
            "/etc/ssl/certs/ca-certificates.crt",
            "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
            "/etc/ssl/cert.pem",
        };
        for(const char* bundle : ca_bundles) {
            if(fs::exists(bundle)) {
                cli.set_ca_cert_path(bundle);
                break;
            }
        }
#endif
        cli.set_follow_location(true);                    // https://raw.github.com 会要求301重定向
        return cli;
    }

    // 计算文件的 SHA256 十六进制小写字符串; 读取或计算失败时返回空串
    inline auto ComputeFileSha256(const fs::path& file) -> std::string {
        std::ifstream in(file, std::ios::binary);
        if(!in) return "";
        // RAII 管理上下文: 分配后到释放间的可抛分配不再泄漏 EVP_MD_CTX(#122 C1)
        struct EvpMdCtxDeleter {
            void operator()(EVP_MD_CTX* p) const noexcept { EVP_MD_CTX_free(p); }
        };
        std::unique_ptr<EVP_MD_CTX, EvpMdCtxDeleter> ctx(EVP_MD_CTX_new());
        if(!ctx) return "";
        bool ok = EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) == 1;
        std::vector<char> buf(kHashBufferSize);
        while(ok && in) {
            in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            const std::streamsize n = in.gcount();
            if(n > 0) {
                ok = EVP_DigestUpdate(ctx.get(), buf.data(), static_cast<size_t>(n)) == 1;
            }
        }
        if(ok && !in.eof()) ok = false; // 非正常结束(读取错误)
        unsigned char md[EVP_MAX_MD_SIZE] = {};
        unsigned int md_len = 0;
        if(ok) ok = EVP_DigestFinal_ex(ctx.get(), md, &md_len) == 1;
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

    // Retry-After 等待上限内的解析结果(#132): -1 表示头缺失或不可解析
    inline auto ParseRetryAfter(const httplib::Response& resp) -> int {
        if(!resp.has_header("Retry-After")) {
            return -1;
        }
        const std::string raw = resp.get_header_value("Retry-After");
        // 仅接受纯秒数(GitHub 限流响应的格式); HTTP-date 形式走默认间隔
        char* end = nullptr;
        const long v = std::strtol(raw.c_str(), &end, 10);
        if(end == nullptr || end == raw.c_str() || *end != '\0' || v <= 0) {
            return -1;
        }
        return static_cast<int>(std::min<long>(v, kRetryAfterCapSec));
    }

    // 解析 X-RateLimit-Reset(unix 秒)为本地 HH:MM 字符串; 失败返回空串(#132)
    inline auto FormatRateLimitReset(const httplib::Response& resp) -> std::string {
        if(!resp.has_header("X-RateLimit-Reset")) {
            return "";
        }
        const std::string raw = resp.get_header_value("X-RateLimit-Reset");
        char* end = nullptr;
        const long long v = std::strtoll(raw.c_str(), &end, 10);
        if(end == nullptr || end == raw.c_str() || *end != '\0' || v <= 0) {
            return "";
        }
        const std::time_t t = static_cast<std::time_t>(v);
        std::tm tm_buf{};
#ifdef _WIN32
        if(localtime_s(&tm_buf, &t) != 0) {
            return "";
        }
#else
        if(localtime_r(&t, &tm_buf) == nullptr) {
            return "";
        }
#endif
        char buf[16] = {};
        if(std::strftime(buf, sizeof(buf), "%H:%M", &tm_buf) == 0) {
            return "";
        }
        return buf;
    }

    /**
     * @brief 从网络上读取数据
     * @param url_host 网络主机
     * @param params 请求链接后缀
     * @param out 成功后输出读取到的数据
     * @return 是否成功
     */
    inline auto ReadHttpDataString(std::string url_host, std::string params, std::string& out, std::pair<std::string, int> proxy = {}) -> bool {
        httplib::Client cli = MakeHttpClient(url_host, proxy);

        // 瞬时性错误(连接失败/403 限流/429/5xx)自动重试: GitHub API 与资产 CDN 在共享 IP 下偶发此类错误
        // 限流场景遵守服务端指示(#132): Retry-After 决定下一轮等待(上限 90s);
        // primary 限流(匿名 60 次/时)提示配额重置时刻, 避免错误排查方向
        httplib::Result res;
        int wait_sec = kHttpRetryDelaySec;
        std::string rate_limit_reset;
        for(int attempt = 1; attempt <= kHttpRetryLimit; attempt++) {
            if(attempt > 1) {
                spdlog::warn("请求 {}{} 失败(第 {} 次), {} 秒后重试", url_host, params, attempt - 1, wait_sec);
                std::this_thread::sleep_for(std::chrono::seconds(wait_sec));
            }
            res = cli.Get(params);
            if(res && res->status == httplib::StatusCode::OK_200) break;
            if(res && res->status != 403 && res->status != 429 && res->status < 500) break; // 非瞬时性错误, 不重试
            if(res) {
                const int retry_after = ParseRetryAfter(*res);
                if(retry_after > 0) {
                    wait_sec = retry_after;
                }
                rate_limit_reset = FormatRateLimitReset(*res);
                if(!rate_limit_reset.empty()) {
                    spdlog::warn("GitHub API 配额已耗尽, 将于 {} 重置(匿名限额 60 次/时)", rate_limit_reset);
                }
            }
        }

        // 限流导致的失败在最终错误中附加重置时刻, 纠正"检查网络和代理"的误导排查方向
        if(res && res->status != httplib::StatusCode::OK_200 && !rate_limit_reset.empty()) {
            spdlog::warn("请求 {}{} 因 GitHub API 配额限流失败, 配额将于 {} 重置", url_host, params, rate_limit_reset);
        }

        if (res) {
            if (res->status == httplib::StatusCode::OK_200) {
                // 响应体上限: 防御异常/恶意服务端返回超大内容(正常 API/文本响应远小于此);
                // 注: httplib 已在内存接收响应, 此处为接受边界而非下载防线
                if(res->body.size() > kMaxResponseBytes) {
                    spdlog::warn("请求 {}{} 响应体过大({} bytes), 已拒绝", url_host, params, res->body.size());
                    out.clear();
                    return false;
                }
                out = res->body;
                return true;
            }
            spdlog::warn("请求 {}{} 返回状态码 {}", url_host, params, res->status);
            out.clear();
            return false;
        }
        else {
            spdlog::warn("请求 {}{} 失败: {}", url_host, params, httplib::to_string(res.error()));
            out.clear();
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
        // exists 用 error_code 重载: 底层错误(权限等)不抛异常; 大小竞态由 file_size 的 ec 分支兜底(#122 B3)
        uint64_t downloaded_bytes = 0;
        std::error_code exists_ec;
        if(fs::exists(tmp_file, exists_ec)) {
            std::error_code size_ec;
            const uint64_t existing = fs::file_size(tmp_file, size_ec);
            if(size_ec) {
                // 文件被占用/权限变化等竞态: 大小不可信, 丢弃残留改为完整重下
                spdlog::warn("读取临时文件大小失败({}), 将删除残留重新下载", size_ec.message());
                std::error_code rm_ec;
                fs::remove(tmp_file, rm_ec);
            }
            else {
                downloaded_bytes = existing;
            }
        }

        // 临时文件超过预期大小(异常残留)时丢弃, 重新完整下载
        if(max_size > 0 && downloaded_bytes > static_cast<uint64_t>(max_size)) {
            spdlog::warn("临时文件大小异常, 将重新下载");
            std::error_code ec;
            fs::remove(tmp_file, ec);
            downloaded_bytes = 0;
        }

        if(max_size <= 0 || downloaded_bytes < static_cast<uint64_t>(max_size)) {
            // max_size<=0 表示无大小预期: 即使已有残留也继续下载(续传), 由 416 自愈兜底错位残留
            // 以 二进制追加模式 打开文件（断点续传关键）
            std::ofstream downfile(tmp_file, std::ios::binary | std::ios::out | std::ios::app);
            if(!downfile.is_open()) {
                spdlog::error("无法打开临时文件写入新版本");
                return false;
            }

            httplib::Client cli = MakeHttpClient(url_host, proxy);
            httplib::Headers headers;
            if(downloaded_bytes > 0) {
                headers.emplace("Range", "bytes=" + std::to_string(downloaded_bytes) + "-");
            }
            headers.emplace("Accept", "application/octet-stream");

            int bad_status = 0; // 非 200/206 的状态码(用于错误提示)
            uint64_t received_bytes = 0; // 本次会话已接收字节数(用于流式大小上限)
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
                received_bytes += data_length;
                // 流式大小上限: 异常/恶意服务端超出发布资产声明大小时立即中止,
                // 不让无限数据撑爆磁盘(大小校验原先要等下载完成后才做)
                if(max_size > 0 && downloaded_bytes + received_bytes > static_cast<uint64_t>(max_size)) {
                    spdlog::error("下载内容超过发布资产声明大小({} 字节), 已中止下载", max_size);
                    return false;
                }
                if(data_length > 0 && downfile.is_open()) {
                    downfile.write(data, data_length);
                    downfile.flush(); // 立即刷入磁盘，不缓存
                }
                // 流状态检查: 磁盘满/写入失败时立即返回 false 中止请求(不再依赖后续尺寸校验兜底)
                return static_cast<bool>(downfile);
            },
            [&](uint64_t len, uint64_t total) {
                uint64_t total_ = downloaded_bytes + total;  // 文件总大小
                uint64_t now_ = downloaded_bytes + len;
                int percent_ = total_ ? static_cast<int>(now_ * 100 / total_) : 0;

                printf("\r %s %d%% ==>  %lld / %lld", (downloaded_bytes > 0) ? "[续传]" : "[下载]", percent_, (long long)now_, (long long)total_);
                return true;
            });
            printf("\n");
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
                // 416 Range Not Satisfiable: 临时文件大小与服务器端实际长度错位(资产被重新发布), 续传永久失败
                // 删除残留让上层重试时整包重下(与 JS 侧 fetch.js 的 416 自愈一致)
                if(res->status == 416 && fs::exists(tmp_file)) {
                    spdlog::warn("断点与服务器端错位(416), 删除残留临时文件后重新完整下载");
                    std::error_code ec;
                    fs::remove(tmp_file, ec);
                }
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
     * @param expected_digest 发布资产声明的摘要（形如 "sha256:<hex>"）
     *        自更新链路 fail-closed(#120): 摘要缺失/格式无法识别/计算失败一律拒绝替换,
     *        不再放行(GitHub API 对 release assets 始终提供 sha256 摘要)
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
            spdlog::error("发布资产未提供SHA256摘要, 拒绝替换(fail-closed)");
            std::error_code ec;
            fs::remove(tmp_file, ec);
            return false;
        }
        const std::string prefix = "sha256:";
        if(expected_digest.rfind(prefix, 0) != 0) {
            spdlog::error("发布资产摘要格式无法识别({}), 拒绝替换(fail-closed)", expected_digest);
            std::error_code ec;
            fs::remove(tmp_file, ec);
            return false;
        }
        const std::string want = expected_digest.substr(prefix.size());
        const std::string got = ComputeFileSha256(tmp_file);
        if(got.empty()) {
            spdlog::error("计算下载文件摘要失败, 拒绝替换(fail-closed)");
            std::error_code ec;
            fs::remove(tmp_file, ec);
            return false;
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

    // 拉起替换脚本: 等待当前进程退出后, 用临时文件替换自身并重启
    // Windows: cmd 脚本(move/start), 全程宽字符 + 文件名引号转义, 兼容含空格/%/&等特殊字符的文件名
    // POSIX:   sh 脚本(mv/exec), 后台 nohup 运行, 单引号转义路径
    // 返回是否成功拉起; 失败时调用方不得假定会自动重启
    inline auto LaunchReplaceAndRestart(const fs::path& self, const fs::path& tmp_file) -> bool {
#ifdef _WIN32
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
        std::wstring p = L"/c ping 127.0.0.1 -n " + std::to_wstring(kReplaceWaitSec + 1) + L" > nul & move /Y ";
        p += quote_name(tmp_file.filename().wstring());
        p += L" ";
        p += quote_name(exe_name.wstring());
        p += L" & start \"\" ";
        p += quote_name(exe_name.wstring());
        // HINSTANCE <= 32 表示失败(权限不足/策略阻止/路径异常); 不校验会导致"提示自动重启"却不会发生且 exit 0
        const auto r = reinterpret_cast<intptr_t>(ShellExecuteW(
            NULL,                       // 父窗口句柄
            L"open",                    // 操作
            L"cmd.exe",                 // 应用程序
            p.c_str(),                  // 参数
            self.parent_path().wstring().c_str(), // 工作目录
            SW_SHOW));                  // 显示方式
        if(r <= 32) {
            spdlog::error("无法启动替换脚本(ShellExecuteW 返回 {}), 请手动用 {} 覆盖 {}", r, tmp_file.string(), self.string());
            return false;
        }
        return true;
#else
        auto sh_quote = [](const std::string& s) {
            std::string out = "'";
            for(char c : s) {
                if(c == '\'') out += "'\\''";
                else out += c;
            }
            out += "'";
            return out;
        };
        // 脚本文件名附加随机后缀: 防止固定名称被抢注(#122 B2)
        const fs::path script = self.parent_path() / (self.filename().string() + ".update" + RandomTempSuffix() + ".sh");
        {
            std::ofstream sh(script, std::ios::binary | std::ios::trunc);
            if(!sh) {
                spdlog::error("无法创建更新脚本 {}", script.string());
                return false;
            }
            sh << "#!/bin/sh\n"
               << "sleep " << kReplaceWaitSec << "\n"
               << "mv -f " << sh_quote(tmp_file.string()) << " " << sh_quote(self.string()) << "\n"
               << "rm -f " << sh_quote(script.string()) << "\n"
               << "exec " << sh_quote(self.string()) << "\n";
        }
        std::error_code ec;
        fs::permissions(script, fs::perms::owner_all, ec);
        // 直接 fork+exec 拉起脚本: 不经 shell, 消除 std::system 的 shell 依赖与注入面(#122 B1)
        const pid_t pid = ::fork();
        if(pid < 0) {
            spdlog::error("无法启动更新脚本(fork 失败), 请手动用 {} 覆盖 {}", tmp_file.string(), self.string());
            return false;
        }
        if(pid == 0) {
            // 子进程: 忽略 SIGHUP(对应原 nohup 语义), 标准输出/错误重定向 /dev/null(对应原 >/dev/null 2>&1)
            ::signal(SIGHUP, SIG_IGN);
            const int devnull = ::open("/dev/null", O_WRONLY);
            if(devnull >= 0) {
                ::dup2(devnull, STDOUT_FILENO);
                ::dup2(devnull, STDERR_FILENO);
                if(devnull > STDERR_FILENO) ::close(devnull);
            }
            ::execl("/bin/sh", "sh", script.string().c_str(), static_cast<char*>(nullptr));
            _exit(127); // exec 失败
        }
        return true;
#endif
    }

    /**
     * @brief 自动更新：下载新版本到临时文件（支持断点续传），校验大小与SHA256后拉起替换脚本并退出
     * @param url_host 下载主机（如 https://github.com）
     * @param params 下载路径
     * @param Self 当前可执行文件路径
     * @param max_size 预期文件大小（0 表示不校验）
     * @param proxy 代理 host:port
     * @param expected_digest 发布资产声明的摘要（形如 "sha256:<hex>"），必须提供且格式正确，
     *                        否则拒绝替换（fail-closed，#120）
     * @return 是否成功（成功后调用方应立即退出等待替换）
     */
    inline auto UpdateProgram(std::string url_host, std::string params, fs::path Self, int64_t max_size, std::pair<std::string, int> proxy, std::string expected_digest = "") -> bool {
        // 下载临时文件: 名称保持稳定以支持跨进程断点续传(重跑时续传依赖同名残留);
        // 完整性由 VerifyDownloadedFile 的 SHA256 fail-closed 校验兜底, 不采用随机名(#122 B2)
        fs::path tmp_file = Self;
        tmp_file += ".new";

        // 下载重试: 资产 CDN 偶发瞬时 403(压力实测 6 轮中 2 轮首试命中), 重试即可恢复;
        // 失败残留的临时文件会自然进入续传/从头重下, 不影响正确性
        bool downloaded = false;
        for(int attempt = 1; attempt <= kDownloadRetryLimit && !downloaded; attempt++) {
            if(attempt > 1) {
                spdlog::warn("下载失败, {} 秒后自动重试(第 {}/{} 次)", kDownloadRetryDelaySec, attempt - 1, kDownloadRetryLimit);
                std::this_thread::sleep_for(std::chrono::seconds(kDownloadRetryDelaySec));
            }
            downloaded = DownloadToFile(url_host, params, proxy, tmp_file, max_size);
        }
        if(!downloaded) {
            return false;
        }
        if(!VerifyDownloadedFile(tmp_file, max_size, expected_digest)) {
            return false;
        }

        spdlog::info("下载完成, 请稍等, 随后自动完成并(无参)重启..");
        if(!LaunchReplaceAndRestart(Self, tmp_file)) {
            return false;
        }
        return true;
    }

}

#endif // !UTILS_HTTP_HPP
