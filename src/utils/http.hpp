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
// windows.h 前定义(未定义时): 排除 WinSock1 等旧接口, 防止独立包含本头的
// 编译单元中 windows.h 预拉入 winsock.h 与 httplib 的 winsock2.h 冲突
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
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
    // 单请求总时长预算(秒): set_read_timeout 只限单次 recv, 滴流服务端每次喂少量数据即可无限续命(#172)
    inline constexpr int kRequestTotalBudgetSec = 120;
    // 下载单请求总时长预算(秒): 发布资产体积大, 比普通 json 请求宽松(#172)
    inline constexpr int kDownloadTotalBudgetSec = 600;
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
    // 下载文件校验失败(如续传累积数据损坏)时从头重下的次数上限(#176)
    inline constexpr int kVerifyRedownloadLimit = 1;
    // 进程内自更新替换(#182/#183): 校验通过后立即在进程内完成改名入位, 不再经 cmd/sh 脚本等待
    // 替换重试次数: 目标名被杀毒/索引服务等短暂占用时重试(仅占用类错误)
    inline constexpr int kReplaceRetryLimit = 5;
    // 替换重试间隔(毫秒)
    inline constexpr int kReplaceRetryDelayMs = 200;
    // 启动清理 .old 残留的重试次数: 上次更新的旧进程退出前短暂占用, 新进程启动时重试删除
    inline constexpr int kOldCleanupRetryLimit = 10;

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
     * @brief 带瞬时错误重试的 GET(单一代理配置), 可选流式响应体上限
     *        瞬时性错误(连接失败/403 限流/429/5xx)自动重试: GitHub API 与资产 CDN 在共享 IP 下偶发
     *        限流场景遵守服务端指示(#132): Retry-After 决定下一轮等待(上限 90s);
     *        primary 限流(匿名 60 次/时)提示配额重置时刻, 避免错误排查方向
     * @param body_out 非空时启用流式接收: 响应体经 ContentReceiver 逐块累计到 *body_out,
     *                 配合 max_bytes 在收满上限时立即断开连接, 不再全量缓冲后检查(#139);
     *                 为空时保持旧全量行为(res->body 持有响应体)
     * @param max_bytes 流式模式下的响应体上限(body_out 非空时生效, 应 > 0)
     * @param oversized 流式模式下超限中止时置 true(出参, 可为空)
     * @param total_budget_sec 单请求总时长预算(秒, 0 = 不限): 单次 recv 超时无法约束滴流服务端(#172),
     *                         超预算经 progress 回调中止连接, 不再重试同通道
     * @param budget_exceeded 超预算中止时置 true(出参, 可为空)
     * @return 最终结果(可能为 nullptr = 连接层失败/流式中止)
     */
    inline auto GetWithRetry(const std::string& url_host, const std::string& params, const std::pair<std::string, int>& proxy,
                             std::string* body_out = nullptr, size_t max_bytes = 0, bool* oversized = nullptr,
                             int total_budget_sec = 0, bool* budget_exceeded = nullptr) -> httplib::Result {
        httplib::Client cli = MakeHttpClient(url_host, proxy);
        httplib::Result res;
        int wait_sec = kHttpRetryDelaySec;
        std::string rate_limit_reset;
        for(int attempt = 1; attempt <= kHttpRetryLimit; attempt++) {
            if(attempt > 1) {
                spdlog::warn("请求 {}{} 失败(第 {} 次), {} 秒后重试", url_host, params, attempt - 1, wait_sec);
                std::this_thread::sleep_for(std::chrono::seconds(wait_sec));
            }
            if(body_out != nullptr) {
                // 流式接收(#139): 每轮重试重置累计状态, 超限立即返回 false 断开连接
                body_out->clear();
                if(oversized != nullptr) {
                    *oversized = false;
                }
                if(budget_exceeded != nullptr) {
                    *budget_exceeded = false;
                }
                uint64_t received_bytes = 0;
                const auto request_start = std::chrono::steady_clock::now();
                res = cli.Get(params, httplib::Headers{},
                [&](const char* data, size_t data_length) {
                    received_bytes += data_length;
                    if(max_bytes > 0 && received_bytes > max_bytes) {
                        if(oversized != nullptr) {
                            *oversized = true;
                        }
                        return false;
                    }
                    body_out->append(data, data_length);
                    return true;
                },
                [&](size_t, size_t) {
                    // 单请求总时长预算(#172): progress 周期回调中检查累计耗时, 超预算立即断开
                    if(total_budget_sec > 0 &&
                       std::chrono::steady_clock::now() - request_start >= std::chrono::seconds(total_budget_sec)) {
                        if(budget_exceeded != nullptr) {
                            *budget_exceeded = true;
                        }
                        return false;
                    }
                    return true;
                });
                // 超限/超预算均为服务端/代理侧确定性异常, 重试必然同样结果
                if((oversized != nullptr && *oversized) || (budget_exceeded != nullptr && *budget_exceeded)) {
                    break;
                }
            }
            else {
                res = cli.Get(params);
            }
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
                else if((res->status == 403 || res->status == 429) && retry_after <= 0) {
                    // 403/429 且无任何限流指示(#253): 非瞬时 —— 多为网络/代理/防火墙拦截
                    // api.github.com 或次级限流, 重试必然同样结果, 与 oversized/budget 的
                    // "确定性异常不重试"原则一致(#172); 避免无谓等待与误导性排查方向
                    spdlog::warn("请求 {}{} 返回 {}, 且未提供限流信息(可能为网络/代理/防火墙拦截或次级限流), 不再重试",
                                 url_host, params, res->status);
                    break;
                }
            }
        }

        // 限流导致的失败在最终错误中附加重置时刻, 纠正"检查网络和代理"的误导排查方向
        if(res && res->status != httplib::StatusCode::OK_200 && !rate_limit_reset.empty()) {
            spdlog::warn("请求 {}{} 因 GitHub API 配额限流失败, 配额将于 {} 重置", url_host, params, rate_limit_reset);
        }
        return res;
    }

    /**
     * @brief 从网络上读取数据
     * @param url_host 网络主机
     * @param params 请求链接后缀
     * @param out 成功后输出读取到的数据
     * @return 是否成功
     */
    inline auto ReadHttpDataString(std::string url_host, std::string params, std::string& out, std::pair<std::string, int> proxy = {}) -> bool {
        // 流式响应体上限(#139): 经 ContentReceiver 逐块累计, 收满 32MB 立即断开连接,
        // 不再让恶意/受损代理把任意大 body 全量灌入内存后才拒绝(原收后检查的缺口)
        std::string body;
        bool oversized = false;
        bool budget_exceeded = false;
        httplib::Result res = GetWithRetry(url_host, params, proxy, &body, kMaxResponseBytes, &oversized,
                                           kRequestTotalBudgetSec, &budget_exceeded);

        // 超限与预算中止均为**服务端/代理侧确定性异常**, 一律不触发代理降级直连重试(#276):
        // 直连同样会被服务端超限拒绝、同样会被滴流拖到超预算 —— 此前只短路 oversized,
        // budget_exceeded 会被误判为「代理不可达」而对同一慢服务端再跑一轮预算(最坏多等一轮 120s)
        if(oversized || budget_exceeded) {
            if(oversized) {
                spdlog::warn("请求 {}{} 响应体超过上限({} bytes), 已断开并拒绝", url_host, params, kMaxResponseBytes);
            }
            else {
                spdlog::warn("请求 {}{} 超过单请求总时长预算({} 秒), 已中止(不降级重试)", url_host, params, kRequestTotalBudgetSec);
            }
            out.clear();
            return false;
        }

        // 失效代理直连降级(#133): 代理进程退出后的系统/环境变量代理残留(中文用户高频场景)
        // 会让经代理请求全部连接层失败, 最坏阻塞数分钟; 此时自动降级直连重试
        if(proxy.second && !res) {
            spdlog::warn("经代理 {}:{} 请求失败(代理不可达), 已尝试直连", proxy.first, proxy.second);
            res = GetWithRetry(url_host, params, {}, &body, kMaxResponseBytes, &oversized,
                               kRequestTotalBudgetSec, &budget_exceeded);
            if(oversized) {
                spdlog::warn("请求 {}{} 响应体超过上限({} bytes), 已断开并拒绝", url_host, params, kMaxResponseBytes);
                out.clear();
                return false;
            }
        }

        // 直连降级后的第二次调用同样可能被预算中止(#172)
        if(budget_exceeded) {
            spdlog::warn("请求 {}{} 超过单请求总时长预算({} 秒), 已中止", url_host, params, kRequestTotalBudgetSec);
        }

        if (res) {
            if (res->status == httplib::StatusCode::OK_200) {
                out = std::move(body);
                return true;
            }
            // 403/429 已在 GetWithRetry 内给出具体诊断(配额重置时刻 / 无限流信息), 此处不重复打印(#256)
            if(res->status != 403 && res->status != 429) {
                spdlog::warn("请求 {}{} 返回状态码 {}", url_host, params, res->status);
            }
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
    // DownloadToFile / VerifyDownloadedFile / ReplaceSelfAndRestart 为 UpdateProgram 的内部辅助

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
            // 单请求总时长预算(#172): set_read_timeout 只限单次 recv, 滴流服务端可无限续命
            const auto request_start = std::chrono::steady_clock::now();
            bool budget_exceeded = false;
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
                // 超预算立即中止下载(断点续传: 已写入部分保留, 重跑时续传)
                if(std::chrono::steady_clock::now() - request_start >= std::chrono::seconds(kDownloadTotalBudgetSec)) {
                    budget_exceeded = true;
                    return false;
                }
                uint64_t total_ = downloaded_bytes + total;  // 文件总大小
                uint64_t now_ = downloaded_bytes + len;
                int percent_ = total_ ? static_cast<int>(now_ * 100 / total_) : 0;

                printf("\r %s %d%% ==>  %lld / %lld", (downloaded_bytes > 0) ? "[续传]" : "[下载]", percent_, (long long)now_, (long long)total_);
                return true;
            });
            printf("\n");
            downfile.close();

            if(!res) {
                if(budget_exceeded) {
                    spdlog::error("下载超过单请求总时长预算({} 秒), 已中止(已下载部分保留供续传)", kDownloadTotalBudgetSec);
                }
                else if(bad_status) {
                    // 416 Range Not Satisfiable: 临时文件大小与服务器端实际长度错位(资产被重新发布), 续传永久失败
                    // #138 回归发现: 416 在上方响应头回调被中止后 Result 无效(operator bool 为 false),
                    // 原 res->status==416 自愈分支不可达(0.15.3 与 0.61 行为一致, 实测复现);
                    // 残留删除移到此处让上层重试时整包重下(与 JS 侧 fetch.js 的 416 自愈一致)
                    if(bad_status == 416 && fs::exists(tmp_file)) {
                        spdlog::warn("断点与服务器端错位(416), 删除残留临时文件后重新完整下载");
                        std::error_code rm_ec;
                        fs::remove(tmp_file, rm_ec);
                    }
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

    // 启动时清理自更新残留(#183): 进程内替换在 Windows 上会把旧版本镜像改名为 .old,
    // 旧进程退出前无法删除, 留待下次启动时重试清理(新进程启动时旧进程通常即将退出);
    // POSIX 的 rename 原子覆盖无 .old 残留, 本函数为空操作。
    // .new 下载残留有意保留供断点续传(#176), 此处不清理。
    inline auto CleanupSelfUpdateResidue(const fs::path& self) -> void {
#ifdef _WIN32
        // 相对路径(裸 wargv[0] 文件名)兜底: 转绝对路径, 不受后续工作目录变化影响
        std::error_code abs_ec;
        const fs::path self_abs = fs::absolute(self, abs_ec);
        const fs::path& self_ref = abs_ec ? self : self_abs;
        fs::path old_file = self_ref;
        old_file += ".old";
        std::error_code exists_ec;
        if(!fs::exists(old_file, exists_ec)) {
            return;
        }
        for(int attempt = 1; attempt <= kOldCleanupRetryLimit; attempt++) {
            std::error_code rm_ec;
            fs::remove(old_file, rm_ec);
            if(!rm_ec) {
                spdlog::info("已清理上次更新残留的旧版本文件");
                return;
            }
            if(attempt < kOldCleanupRetryLimit) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kReplaceRetryDelayMs));
            }
        }
        spdlog::debug("旧版本残留 {} 暂无法删除(可能仍被占用), 留待下次启动清理", old_file.filename().string());
#else
        (void)self; // POSIX rename 原子覆盖, 无 .old 残留
#endif
    }

    // 回滚兜底(#271): rename 失败(占用/权限)时改用复制恢复 —— 复制对句柄占用的容忍度更高,
    // 尽量保证 <self> 存在, 避免出现「只剩 .old、程序无法启动」的不可恢复状态。
    // 仅作为 rename 失败后的第二选择; 复制成功后保留 .old(不删除), 交由下次启动的残留清理处理。
    inline auto RestoreSelfBestEffort(const fs::path& from, const fs::path& self_ref) -> bool {
        std::error_code ec;
        fs::rename(from, self_ref, ec);
        if(!ec) {
            return true;
        }
        std::error_code cp_ec;
        fs::copy_file(from, self_ref, fs::copy_options::overwrite_existing, cp_ec);
        if(cp_ec) {
            return false;
        }
        std::error_code ex_ec;
        return fs::exists(self_ref, ex_ec) && !ex_ec;
    }

    // 进程内替换自身并重启新版本(#182/#183):
    // Windows: 重命名运行中 exe 为 .old → 移动 .new 立即入位 → CreateProcessW 重启。
    //          全程不经 cmd, 消除 cmd /c 上下文 %% 转义不折叠与路径含 % 展开破坏更新的缺陷(#182);
    //          .new 在 SHA256 校验通过后毫秒级入位, 消除原「校验→等待5秒→替换」的 TOCTOU 窗口(#183)。
    // POSIX:   fs::permissions 补执行位 → fs::rename 原子覆盖运行中镜像(内核为运行中的旧进程保留 inode)
    //          → setsid+fork+execl 全脱离重启。
    // 返回 true ⇔ 新版本已入位(重启拉起失败仅告警, 仍返回 true, 新版本下次手动启动生效);
    // 返回 false ⇔ 替换失败(原程序完好, 调用方可继续运行)。
    inline auto ReplaceSelfAndRestart(const fs::path& self, const fs::path& tmp_file) -> bool {
        // 相对路径(裸 wargv[0] 文件名)兜底: Windows 对相对/裸应用名的 CreateProcessW
        // 会走系统目录搜索, 必须转绝对路径
        std::error_code abs_ec;
        const fs::path self_abs = fs::absolute(self, abs_ec);
        const fs::path& self_ref = abs_ec ? self : self_abs;

        // 前置检查: 下载文件必须存在且非空(防止 .new 意外丢失时误判替换失败原因)
        std::error_code size_ec;
        const uint64_t new_size = fs::file_size(tmp_file, size_ec);
        if(size_ec || new_size == 0) {
            spdlog::error("更新临时文件 {} 缺失或为空, 无法替换", tmp_file.filename().string());
            return false;
        }
#ifdef _WIN32
        // Windows 允许重命名运行中的 exe(旧进程的镜像映射不受影响)但不允许覆盖:
        // 依序 self → .old(腾位), .new → self(入位), 入位失败回滚
        fs::path old_file = self_ref;
        old_file += ".old";
        std::error_code ec;
        bool renamed = false;
        for(int attempt = 1; attempt <= kReplaceRetryLimit; attempt++) {
            ec.clear();
            fs::rename(self_ref, old_file, ec);
            if(!ec) {
                renamed = true;
                break;
            }
            // 仅对占用类错误(杀毒/索引服务短暂锁定等)重试; 其余(权限/只读卷)为确定性失败
            const int err = static_cast<int>(ec.value());
            if(err != ERROR_ACCESS_DENIED && err != ERROR_SHARING_VIOLATION) {
                break;
            }
            if(attempt < kReplaceRetryLimit) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kReplaceRetryDelayMs));
            }
        }
        if(!renamed) {
            spdlog::error("重命名当前程序失败({}), 请手动用 {} 覆盖 {}", ec.message(), tmp_file.string(), self_ref.string());
            return false;
        }
        // .new 入位前强制落盘(#243): 下载仅刷到 OS 缓存(ofstream::flush 为 C 库语义),
        // 断电可致入位的新 exe 半截/空 —— 与 #171 的 rename 前落盘范式一致
        std::error_code sync_ec;
        if(!FlushToDisk(tmp_file, sync_ec)) {
            spdlog::error("更新文件落盘失败({}), 已回滚原程序, 请手动用 {} 覆盖 {}",
                sync_ec.message(), tmp_file.string(), self_ref.string());
            std::error_code rb_ec;
            if(RestoreSelfBestEffort(old_file, self_ref)) {
                spdlog::error("更新文件落盘失败({}), 已回滚原程序, 请手动用 {} 覆盖 {}", sync_ec.message(), tmp_file.string(), self_ref.string());
            }
            else {
                spdlog::error("回滚亦失败, 原程序已移至 {}, 请手动恢复", old_file.string());
            }
            return false;
        }
        // .new → self 入位: 与 self → .old 对称的占用类重试(#243) —— 杀软/索引服务扫描 .new 时
        // 首试可能失败, 此前无重试即直接回滚, 无谓降低更新成功率
        for(int attempt = 1; attempt <= kReplaceRetryLimit; attempt++) {
            ec.clear();
            fs::rename(tmp_file, self_ref, ec);
            if(!ec) {
                break;
            }
            const int err = static_cast<int>(ec.value());
            if(err != ERROR_ACCESS_DENIED && err != ERROR_SHARING_VIOLATION) {
                break;
            }
            if(attempt < kReplaceRetryLimit) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kReplaceRetryDelayMs));
            }
        }
        if(ec) {
            // 回滚: 恢复原程序保持可运行; 回滚也失败时提示手动恢复位置
            std::error_code rb_ec;
            if(RestoreSelfBestEffort(old_file, self_ref)) {
                spdlog::error("新版本入位失败({}), 已回滚原程序, 请手动用 {} 覆盖 {}", ec.message(), tmp_file.string(), self_ref.string());
            }
            else {
                spdlog::error("新版本入位失败({})且回滚失败, 原程序已移至 {}, 请手动恢复", ec.message(), old_file.string());
            }
            return false;
        }
        spdlog::info("新版本已就位, 正在重启..");
        // CREATE_NEW_CONSOLE: 保留原 cmd start 的独立新控制台窗口语义;
        // lpCommandLine 需可变缓冲(CreateProcessW 可能写回); 不经 cmd,
        // 路径含 %/空格/中文 等无需任何转义(#182)
        std::wstring cmd_line = L"\"" + self_ref.wstring() + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        const BOOL started = CreateProcessW(nullptr, cmd_line.data(), nullptr, nullptr, FALSE,
                                             CREATE_NEW_CONSOLE, nullptr, self_ref.parent_path().wstring().c_str(),
                                             &si, &pi);
        if(!started) {
            const int err = static_cast<int>(GetLastError());
            spdlog::warn("新版本已就位, 但自动重启失败(错误码 {}), 请手动启动 {}", err, self_ref.string());
            return true;
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
#else
        // 下载的 .new 由 ofstream 创建(默认 0644): rename 不改变权限位,
        // 不补执行位则重启 exec 以 EACCES 失败
        std::error_code perm_ec;
        fs::permissions(tmp_file,
                        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add, perm_ec);
        if(perm_ec) {
            spdlog::error("无法为更新文件添加执行权限({}), 请手动用 {} 覆盖 {}", perm_ec.message(), tmp_file.string(), self_ref.string());
            return false;
        }
        // rename 前落盘(#243): 与 Windows 分支一致, 避免断电后新镜像为半截/空
        std::error_code sync_ec;
        if(!FlushToDisk(tmp_file, sync_ec)) {
            spdlog::error("更新文件落盘失败({}), 请手动用 {} 覆盖 {}", sync_ec.message(), tmp_file.string(), self_ref.string());
            return false;
        }
        std::error_code ec;
        fs::rename(tmp_file, self_ref, ec);
        if(ec) {
            spdlog::error("替换失败({}), 请手动用 {} 覆盖 {}", ec.message(), tmp_file.string(), self_ref.string());
            return false;
        }
        spdlog::info("新版本已就位, 正在重启..");
        // 全脱离重启: setsid 建立新会话不再受父进程终端退出影响;
        // stdin/stdout/stderr 重定向 /dev/null, 消除与原终端的输入/输出竞争
        const pid_t pid = ::fork();
        if(pid < 0) {
            spdlog::warn("新版本已就位, 但 fork 重启失败, 请手动启动 {}", self_ref.string());
            return true;
        }
        if(pid == 0) {
            ::setsid();
            ::signal(SIGHUP, SIG_IGN);
            const int devnull = ::open("/dev/null", O_RDWR);
            if(devnull >= 0) {
                ::dup2(devnull, STDIN_FILENO);
                ::dup2(devnull, STDOUT_FILENO);
                ::dup2(devnull, STDERR_FILENO);
                if(devnull > STDERR_FILENO) {
                    ::close(devnull);
                }
            }
            ::execl(self_ref.c_str(), self_ref.c_str(), static_cast<char*>(nullptr));
            _exit(127); // exec 失败
        }
        return true;
#endif
    }

    /**
     * @brief 自动更新：下载新版本到临时文件（支持断点续传），校验大小与SHA256（不通过删临时文件从头重下，#176），
     *        校验通过后进程内立即替换自身并重启（#182/#183）
     * @param url_host 下载主机（如 https://github.com）
     * @param params 下载路径
     * @param Self 当前可执行文件路径
     * @param max_size 预期文件大小（0 表示不校验）
     * @param proxy 代理 host:port
     * @param expected_digest 发布资产声明的摘要（形如 "sha256:<hex>"），必须提供且格式正确，
     *                        否则拒绝替换（fail-closed，#120）
     * @return 是否成功（成功后调用方应立即退出, 新版本已入位并由替换逻辑负责重启）
     */
    inline auto UpdateProgram(std::string url_host, std::string params, fs::path Self, int64_t max_size, std::pair<std::string, int> proxy, std::string expected_digest = "") -> bool {
        // 下载临时文件: 名称保持稳定以支持跨进程断点续传(重跑时续传依赖同名残留);
        // 完整性由 VerifyDownloadedFile 的 SHA256 fail-closed 校验兜底, 不采用随机名(#122 B2)
        fs::path tmp_file = Self;
        tmp_file += ".new";

        // 校验失败重下: 断点续传累积的数据损坏导致校验不通过时, 删临时文件从头重下(#176),
        // 重下上限 kVerifyRedownloadLimit 次, 仍失败则放弃(fail-closed 语义不变, 减少用户手动重跑)
        bool verified = false;
        for(int round = 0; round <= kVerifyRedownloadLimit && !verified; round++) {
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

            // 失效代理直连降级(#133): 经代理下载全部失败时降级直连重试一轮
            // (与 ReadHttpDataString 的降级策略对应; 代理进程退出后的配置残留场景)
            if(!downloaded && proxy.second) {
                spdlog::warn("经代理 {}:{} 下载失败(代理不可达), 已尝试直连", proxy.first, proxy.second);
                for(int attempt = 1; attempt <= kDownloadRetryLimit && !downloaded; attempt++) {
                    if(attempt > 1) {
                        spdlog::warn("直连下载失败, {} 秒后自动重试(第 {}/{} 次)", kDownloadRetryDelaySec, attempt - 1, kDownloadRetryLimit);
                        std::this_thread::sleep_for(std::chrono::seconds(kDownloadRetryDelaySec));
                    }
                    downloaded = DownloadToFile(url_host, params, {}, tmp_file, max_size);
                }
            }
            if(!downloaded) {
                return false;
            }
            // 校验失败时 VerifyDownloadedFile 已删除临时文件, 首轮失败进入下一轮从头重下(#176)
            verified = VerifyDownloadedFile(tmp_file, max_size, expected_digest);
        }
        if(!verified) {
            return false;
        }

        spdlog::info("下载完成, 正在替换为新版本..");
        if(!ReplaceSelfAndRestart(Self, tmp_file)) {
            return false;
        }
        return true;
    }

}

#endif // !UTILS_HTTP_HPP
