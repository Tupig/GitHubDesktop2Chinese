# third_party 三方依赖清单

本目录存放 GitHubDesktop2Chinese 的全部第三方依赖，**全部随仓库提交**（含 Windows 版 OpenSSL 静态库），克隆后无需联网下载依赖即可构建。

依赖来源政策：仅使用上游官方 GitHub 仓库 / 官方 Release 资产，不引入外部镜像或非官方预编译产物（CI 使用的 GitHub Actions 同样仅限 `actions/*` 与 `github/*` 官方维护的 Action）。

## 目录结构

```text
third_party/
├── include/          # Header-only 库（CLI11 / cpp-httplib / nlohmann/json / spdlog / WinReg）
└── openssl/          # Windows 版 OpenSSL 头文件 + 静态库（3.5.9 LTS，MSVC x64）
    ├── include/
    └── lib/
```

## 版本清单

| 组件 | 版本 | 上游（官方 GitHub） | 许可证 | 仓库内位置 |
| --- | --- | --- | --- | --- |
| CLI11 | 2.4.1 | [CLIUtils/CLI11](https://github.com/CLIUtils/CLI11) | BSD-3-Clause | `include/CLI/` |
| cpp-httplib | 0.15.3 | [yhirose/cpp-httplib](https://github.com/yhirose/cpp-httplib) | MIT | `include/http/` |
| nlohmann/json | 3.11.3 | [nlohmann/json](https://github.com/nlohmann/json) | MIT | `include/nlohmann/` |
| spdlog | 1.13.0 | [gabime/spdlog](https://github.com/gabime/spdlog) | MIT | `include/spdlog/` |
| WinReg | 2024-02-22 快照（上游无版本号） | [GiovanniDicanio/WinReg](https://github.com/GiovanniDicanio/WinReg) | MIT | `include/WinReg/` |
| OpenSSL（Windows 静态库） | 3.5.9 LTS（2026-09-29 发布） | [openssl/openssl](https://github.com/openssl/openssl)（tag `openssl-3.5.9`） | Apache-2.0 | `openssl/{include,lib}` |

> 版本以各头文件内版本宏为准（如 `CPPHTTPLIB_VERSION`、`NLOHMANN_JSON_VERSION_*`、`SPDLOG_VER_*`、`OPENSSL_VERSION_TEXT`）；WinReg 无版本宏，以其头部注释的更新日期标识。

## 更新策略

- **只升级、不随手换源**：新版本必须来自上表列出的官方 GitHub 仓库或其官方 Release；禁止引入镜像站、第三方预编译包。
- **Header-only 库**：用上游官方发布内容替换 `include/` 对应子目录，替换后以版本宏核对版本，并同步更新本表。
- **OpenSSL**：仅跟随 **LTS** 分支升级；Windows 静态库由维护者本地构建后连同头文件一起提交（构建步骤见下）。升级后需同步更新：`third_party/openssl/`、本文件版本表、`README.md`、`SECURITY.md`、`docs/wiki/编译指南.md`。
- **macOS / Linux**：不使用本目录的 OpenSSL——macOS 产物由 CI 基于官方源码构建静态库（见工作流），Linux 使用系统包管理器安装（如 `libssl-dev`）。

## Windows OpenSSL 静态库构建步骤（维护者参考）

工具链：Visual Studio（MSVC x64）+ 便携 [Strawberry Perl](https://github.com/StrawberryPerl/Perl-Dist-Strawberry)（官方 GitHub Release 下载 portable 版）；无需 NASM（`no-asm` 纯 C 实现，避开 nasm.us 非 GitHub 渠道）。

```powershell
# 1. 官方源码: https://github.com/openssl/openssl/releases/download/openssl-3.5.9/openssl-3.5.9.tar.gz
tar -xzf openssl-3.5.9.tar.gz
cd openssl-3.5.9

# 2. 在 "x64 Native Tools Command Prompt"(或先 call vcvars64.bat)中,
#    PATH 前置便携 Perl 的 perl\bin 后执行:
perl Configure VC-WIN64A no-shared no-tests no-apps no-zlib no-asm --prefix=<安装目录> --openssldir=<安装目录>\ssl
nmake
nmake install_sw

# 3. 用 <安装目录>\include 与 <安装目录>\lib 整体替换 third_party\openssl\{include,lib} 后提交
```

构建选项与产物约定：

- `no-shared`：静态库（`libssl.lib` / `libcrypto.lib`），链接进单文件 exe
- `no-tests` / `no-apps`：跳过测试与命令行工具，缩短构建时间
- `no-zlib`：与 CI 的 macOS 静态库构建保持一致，不引入 zlib 依赖
- `no-asm`：纯 C 实现；NASM 官方二进制仅发布于 nasm.us（非 GitHub），按仓库来源政策不引入，对本工具的 TLS 下载场景性能影响可忽略
