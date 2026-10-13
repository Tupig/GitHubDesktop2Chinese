# third_party 三方依赖清单

**简体中文** | [English](#third-party-dependencies-english)

本目录存放 GitHubDesktop2Chinese 的全部第三方依赖，**全部随仓库提交**（含 Windows / macOS / Linux 版 OpenSSL 静态库），克隆后无需联网下载依赖即可构建。

依赖来源政策：仅使用上游官方 GitHub 仓库 / 官方 Release 资产，不引入外部镜像或非官方预编译产物（CI 使用的 GitHub Actions 仅限 `actions/*` 与 `github/*` 官方维护的 Action，外加 Release 代码签名专用、SignPath 官方维护的 `signpath/github-action-submit-signing-request`）。

## 目录结构

```text
third_party/
├── include/              # Header-only 库（CLI11 / cpp-httplib / nlohmann/json / spdlog / WinReg）
├── openssl/              # Windows 版 OpenSSL 头文件 + 静态库（3.5.9 LTS，MSVC x64）
│   ├── include/
│   └── lib/
├── openssl-macos-x64/    # macOS x64 版 OpenSSL 头文件 + 静态库（3.5.9 LTS）
│   ├── include/
│   └── lib/
├── openssl-macos-arm64/  # macOS arm64 版 OpenSSL 头文件 + 静态库（3.5.9 LTS）
│   ├── include/
│   └── lib/
└── openssl-linux-x64/    # Linux x64 版 OpenSSL 头文件 + 静态库（3.5.9 LTS，-fPIC）
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
| OpenSSL（macOS 静态库 x64/arm64） | 3.5.9 LTS（2026-09-29 发布） | [openssl/openssl](https://github.com/openssl/openssl)（tag `openssl-3.5.9`） | Apache-2.0 | `openssl-macos-x64/`、`openssl-macos-arm64/` |
| OpenSSL（Linux 静态库 x64） | 3.5.9 LTS（2026-09-29 发布） | [openssl/openssl](https://github.com/openssl/openssl)（tag `openssl-3.5.9`） | Apache-2.0 | `openssl-linux-x64/` |

> 版本以各头文件内版本宏为准（如 `CPPHTTPLIB_VERSION`、`NLOHMANN_JSON_VERSION_*`、`SPDLOG_VER_*`、`OPENSSL_VERSION_TEXT`）；WinReg 无版本宏，以其头部注释的更新日期标识。

## 更新策略

- **只升级、不随手换源**：新版本必须来自上表列出的官方 GitHub 仓库或其官方 Release；禁止引入镜像站、第三方预编译包。
- **Header-only 库**：用上游官方发布内容替换 `include/` 对应子目录，替换后以版本宏核对版本，并同步更新本表。
- **OpenSSL**：仅跟随 **LTS** 分支升级；三平台静态库（Windows / macOS / Linux）均由主工作流的 vendor 任务产出后随仓库提交（触发与下载步骤见下；Windows 的维护者本地构建步骤保留作备选）。升级后需同步更新：`third_party/openssl/`、`third_party/openssl-macos-{x64,arm64}/`、`third_party/openssl-linux-x64/`、本文件版本表、`README.md`、`SECURITY.md`、`docs/wiki/编译指南.md`。

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

## macOS OpenSSL 静态库生成步骤（维护者参考）

macOS 双架构静态库由主工作流 [.github/workflows/ghdesktop2chinese.yml](../.github/workflows/ghdesktop2chinese.yml) 的 **产库 OpenSSL (macOS)** 任务统一产出（PR 打 `vendor-openssl` 标签或手动 `type=vendor` 触发，不参与日常 CI）：

1. 在 PR 上打 `vendor-openssl` 标签（labeled 事件触发），或在 GitHub Actions 页面手动运行主工作流并选择 `type=vendor`；`openssl-version` 输入默认 `3.5.9`（仅 LTS）。
2. 运行完成后，从两个 artifact（`openssl-macos-x64` / `openssl-macos-arm64`，保留 7 天）下载产物：
   `gh run download <run-id> -n openssl-macos-x64 -D third_party/openssl-macos-x64`（arm64 同理）。
3. 确认 `include/openssl/ssl.h`、`lib/libssl.a`、`lib/libcrypto.a` 齐备后随仓库提交。

构建选项与产物约定（与主构建工作流原内联构建一致）：

- 目标 `darwin64-x86_64-cc` / `darwin64-arm64-cc`，在 `macos-15-intel` runner 上产出，与 CI 构建环境同源
- `no-shared`（静态库 `libssl.a` / `libcrypto.a`）、`no-tests` / `no-apps`、`no-zlib`（与 Windows 构建一致，不引入 zlib 依赖）
- `MACOSX_DEPLOYMENT_TARGET=11.0`：arm64 产物最低支持 macOS 11，双架构部署目标统一

## Linux OpenSSL 静态库生成步骤（维护者参考）

Linux x64 静态库由主工作流的 **产库 OpenSSL (linux x64)** 任务产出（触发方式与 macOS 相同，见上节）：

1. 在 PR 上打 `vendor-openssl` 标签，或手动运行主工作流并选择 `type=vendor`。
2. 运行完成后下载 artifact（`openssl-linux-x64`，保留 7 天）：
   `gh run download <run-id> -n openssl-linux-x64 -D third_party/openssl-linux-x64`。
3. 确认 `include/openssl/ssl.h`、`lib/libssl.a`、`lib/libcrypto.a` 齐备后随仓库提交。

构建选项与产物约定：目标 `linux-x86_64`，`-fPIC`（位置无关代码，与 PIE 主程序一致）、`no-shared` / `no-tests` / `no-apps` / `no-zlib`（与 Windows/macOS 构建一致）、`--libdir=lib`（统一 `lib/` 安装布局），在 `ubuntu-24.04` runner 上产出。

---

# Third-Party Dependencies (English)

[简体中文](#third_party-三方依赖清单) | **English**

This directory holds all third-party dependencies of GitHubDesktop2Chinese. **Everything is committed to the repository** (including the Windows, macOS and Linux OpenSSL static libraries), so a clone can be built without downloading any dependencies.

Dependency provenance policy: only upstream official GitHub repositories / official release assets are used; external mirrors and unofficial prebuilt artifacts are never introduced (the GitHub Actions used by CI are limited to the officially maintained `actions/*` and `github/*` Actions, plus `signpath/github-action-submit-signing-request`, maintained by SignPath and used solely for release code signing).

## Directory Layout

```text
third_party/
├── include/              # Header-only libraries (CLI11 / cpp-httplib / nlohmann/json / spdlog / WinReg)
├── openssl/              # Windows OpenSSL headers + static libraries (3.5.9 LTS, MSVC x64)
│   ├── include/
│   └── lib/
├── openssl-macos-x64/    # macOS x64 OpenSSL headers + static libraries (3.5.9 LTS)
│   ├── include/
│   └── lib/
├── openssl-macos-arm64/  # macOS arm64 OpenSSL headers + static libraries (3.5.9 LTS)
│   ├── include/
│   └── lib/
└── openssl-linux-x64/    # Linux x64 OpenSSL headers + static libraries (3.5.9 LTS, -fPIC)
    ├── include/
    └── lib/
```

## Version Manifest

| Component | Version | Upstream (official GitHub) | License | In-repo location |
| --- | --- | --- | --- | --- |
| CLI11 | 2.4.1 | [CLIUtils/CLI11](https://github.com/CLIUtils/CLI11) | BSD-3-Clause | `include/CLI/` |
| cpp-httplib | 0.15.3 | [yhirose/cpp-httplib](https://github.com/yhirose/cpp-httplib) | MIT | `include/http/` |
| nlohmann/json | 3.11.3 | [nlohmann/json](https://github.com/nlohmann/json) | MIT | `include/nlohmann/` |
| spdlog | 1.13.0 | [gabime/spdlog](https://github.com/gabime/spdlog) | MIT | `include/spdlog/` |
| WinReg | 2024-02-22 snapshot (upstream has no version number) | [GiovanniDicanio/WinReg](https://github.com/GiovanniDicanio/WinReg) | MIT | `include/WinReg/` |
| OpenSSL (Windows static libraries) | 3.5.9 LTS (released 2026-09-29) | [openssl/openssl](https://github.com/openssl/openssl) (tag `openssl-3.5.9`) | Apache-2.0 | `openssl/{include,lib}` |
| OpenSSL (macOS static libraries x64/arm64) | 3.5.9 LTS (released 2026-09-29) | [openssl/openssl](https://github.com/openssl/openssl) (tag `openssl-3.5.9`) | Apache-2.0 | `openssl-macos-x64/`, `openssl-macos-arm64/` |
| OpenSSL (Linux static libraries x64) | 3.5.9 LTS (released 2026-09-29) | [openssl/openssl](https://github.com/openssl/openssl) (tag `openssl-3.5.9`) | Apache-2.0 | `openssl-linux-x64/` |

> Versions are authoritative per the version macros inside each header (e.g. `CPPHTTPLIB_VERSION`, `NLOHMANN_JSON_VERSION_*`, `SPDLOG_VER_*`, `OPENSSL_VERSION_TEXT`); WinReg has no version macro and is identified by the update date in its header comment.

## Update Policy

- **Upgrade only, never swap sources casually**: new versions must come from the official GitHub repositories listed above or their official releases; mirrors and third-party prebuilt packages are forbidden.
- **Header-only libraries**: replace the corresponding subdirectory under `include/` with the upstream official release, verify the version via the version macros after replacing, and update the table above accordingly.
- **OpenSSL**: follow **LTS** branches only; the static libraries on all three platforms (Windows / macOS / Linux) are produced by the main workflow's vendor jobs and committed (trigger and download steps below; the maintainer's local Windows build steps below remain as a fallback). After upgrading, update in sync: `third_party/openssl/`, `third_party/openssl-macos-{x64,arm64}/`, `third_party/openssl-linux-x64/`, the version table in this file, `README.md`, `SECURITY.md`, and `docs/wiki/编译指南.md`.

## Windows OpenSSL Static Library Build Steps (maintainer reference)

Toolchain: Visual Studio (MSVC x64) + portable [Strawberry Perl](https://github.com/StrawberryPerl/Perl-Dist-Strawberry) (portable edition downloaded from the official GitHub Release); NASM is not needed (`no-asm` pure-C build avoids the non-GitHub nasm.us channel).

```powershell
# 1. Official source: https://github.com/openssl/openssl/releases/download/openssl-3.5.9/openssl-3.5.9.tar.gz
tar -xzf openssl-3.5.9.tar.gz
cd openssl-3.5.9

# 2. In an "x64 Native Tools Command Prompt" (or after calling vcvars64.bat),
#    with the portable Perl's perl\bin prepended to PATH, run:
perl Configure VC-WIN64A no-shared no-tests no-apps no-zlib no-asm --prefix=<install dir> --openssldir=<install dir>\ssl
nmake
nmake install_sw

# 3. Replace third_party\openssl\{include,lib} wholesale with <install dir>\include and <install dir>\lib, then commit
```

Build options and artifact conventions:

- `no-shared`: static libraries (`libssl.lib` / `libcrypto.lib`), linked into the single-file executable
- `no-tests` / `no-apps`: skip tests and command-line tools to shorten build time
- `no-zlib`: consistent with the CI macOS static library build; avoids introducing a zlib dependency
- `no-asm`: pure-C implementation; official NASM binaries are published only on nasm.us (not GitHub), so per the repository provenance policy they are not introduced — the performance impact for this tool's TLS download scenario is negligible

## macOS OpenSSL Static Library Generation Steps (maintainer reference)

Both macOS architectures are produced by the **产库 OpenSSL (macOS)** jobs of the main workflow [.github/workflows/ghdesktop2chinese.yml](../.github/workflows/ghdesktop2chinese.yml) (triggered by the `vendor-openssl` label on a PR or manual `type=vendor`, not part of day-to-day CI):

1. Add the `vendor-openssl` label to a PR (labeled event), or run the main workflow manually from the GitHub Actions page with `type=vendor`; the `openssl-version` input defaults to `3.5.9` (LTS only).
2. Once the run completes, download the two artifacts (`openssl-macos-x64` / `openssl-macos-arm64`, retained for 7 days):
   `gh run download <run-id> -n openssl-macos-x64 -D third_party/openssl-macos-x64` (likewise for arm64).
3. Verify that `include/openssl/ssl.h`, `lib/libssl.a` and `lib/libcrypto.a` are present, then commit them to the repository.

Build options and artifact conventions (identical to the previous inline build in the main workflow):

- Targets `darwin64-x86_64-cc` / `darwin64-arm64-cc`, produced on the `macos-15-intel` runner — same toolchain as the CI builds
- `no-shared` (static libraries `libssl.a` / `libcrypto.a`), `no-tests` / `no-apps`, `no-zlib` (consistent with the Windows build; avoids introducing a zlib dependency)
- `MACOSX_DEPLOYMENT_TARGET=11.0`: arm64 artifacts support macOS 11 at minimum, unifying the deployment target across both architectures

## Linux OpenSSL Static Library Generation Steps (maintainer reference)

The Linux x64 static library is produced by the **产库 OpenSSL (linux x64)** job of the main workflow (triggered the same way as macOS, see the section above):

1. Add the `vendor-openssl` label to a PR, or run the main workflow manually with `type=vendor`.
2. Once the run completes, download the artifact (`openssl-linux-x64`, retained for 7 days):
   `gh run download <run-id> -n openssl-linux-x64 -D third_party/openssl-linux-x64`.
3. Verify that `include/openssl/ssl.h`, `lib/libssl.a` and `lib/libcrypto.a` are present, then commit them to the repository.

Build options and artifact conventions: target `linux-x86_64`, `-fPIC` (position-independent code, matching the PIE main program), `no-shared` / `no-tests` / `no-apps` / `no-zlib` (consistent with the Windows/macOS builds), `--libdir=lib` (unified `lib/` install layout), produced on the `ubuntu-24.04` runner.
