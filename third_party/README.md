# third_party 三方依赖清单

**简体中文** | [English](#third-party-dependencies-english)

本目录存放 TuPigGHD2Chinese 的全部第三方依赖，**全部随仓库提交**（含 Windows / macOS / Linux 版 OpenSSL 静态库），克隆后无需联网下载依赖即可构建。

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
- **OpenSSL**：仅跟随 **LTS** 分支升级；三平台静态库（Windows / macOS / Linux）均由主工作流的 vendor 任务产出。产库成功后由 **OpensslVendorPR** 任务自动组装 `third_party` 并**开启入库 PR**（校验与流程见下节），由维护者审查后 squash 合并；**所产版本强制为 openssl/openssl 官方最新 Release**，非最新版本直接拒绝开 PR。Windows 的维护者本地构建步骤保留作备选。升级合并后需同步更新：本文件版本表、`README.md`、`SECURITY.md`、`docs/wiki/编译指南.md`。

## 产库自动入库流程（OpenSSL）

触发方式：PR 打 `vendor-openssl` 标签、或手动运行主工作流选择 `type=vendor`（`openssl-version` 留空自动取官方最新 Release，不锁 3.x）。

1. 四平台产库任务（Windows / Linux / macOS x64 / macOS arm64）全部成功后，**OpensslVendorPR** 任务自动执行：
   - 校验所产版本为 openssl/openssl **官方最新 Release**（GitHub API `releases/latest`，非最新则拒绝开 PR）；
   - 校验四平台源码包 SHA256 完全一致（各自独立下载、独立溯源）；
   - 校验产物版本宏（`OPENSSL_VERSION_STR`）与所产版本一致、`libssl` / `libcrypto` 齐备后组装 `third_party` 四目录；
   - 与仓库现有版本相同则自动跳过（不开重复 PR）；同一 run 重跑有分支存在性防御。
2. 自动创建入库 PR（分支 `vendor/openssl-<版本>-r<run-id>`，含四平台溯源 SHA256 表与维护者审查清单）。注意：由 `GITHUB_TOKEN` 创建的 PR **不会触发 CI**（GitHub 防递归机制），审查依赖维护者按清单本地验证。
3. 维护者审查清单（见 PR 描述）：四产库任务全绿、SHA256 与 openssl.org 交叉核对、本地构建 + `version_test` + 冒烟通过、文档版本同步，确认后 **squash 合并**入库。

## Windows OpenSSL 静态库构建步骤（维护者备选参考）

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

## macOS OpenSSL 静态库生成步骤（维护者备选参考）

macOS 双架构静态库由主工作流 [.github/workflows/tupigghd2chinese.yml](../.github/workflows/tupigghd2chinese.yml) 的 **产库 OpenSSL (macOS)** 任务统一产出（PR 打 `vendor-openssl` 标签或手动 `type=vendor` 触发，不参与日常 CI）：

1. 在 PR 上打 `vendor-openssl` 标签（labeled 事件触发），或在 GitHub Actions 页面手动运行主工作流并选择 `type=vendor`；`openssl-version` 输入留空自动取官方最新 Release。
2. 运行完成后产物经**产库自动入库流程**（见上节）组装开 PR；手动备选：从 artifact（`openssl-macos-x64` / `openssl-macos-arm64`，保留 7 天）下载产物：
   `gh run download <run-id> -n openssl-macos-x64 -D third_party/openssl-macos-x64`（arm64 同理）。
3. 确认 `include/openssl/ssl.h`、`lib/libssl.a`、`lib/libcrypto.a` 齐备后随仓库提交。

构建选项与产物约定（与主构建工作流原内联构建一致）：

- 目标 `darwin64-x86_64-cc` 在 `macos-15-intel` runner 上产出；`darwin64-arm64-cc` 在 `macos-15`（原生 Apple Silicon）runner 上产出，与 CI 构建环境同源
- `no-shared`（静态库 `libssl.a` / `libcrypto.a`）、`no-tests` / `no-apps`、`no-zlib`（与 Windows 构建一致，不引入 zlib 依赖）
- `MACOSX_DEPLOYMENT_TARGET=11.0`：arm64 产物最低支持 macOS 11，双架构部署目标统一

## Linux OpenSSL 静态库生成步骤（维护者备选参考）

Linux x64 静态库由主工作流的 **产库 OpenSSL (linux x64)** 任务产出（触发方式与 macOS 相同，见上节）：

1. 在 PR 上打 `vendor-openssl` 标签，或手动运行主工作流并选择 `type=vendor`。
2. 运行完成后产物经**产库自动入库流程**（见上节）组装开 PR；手动备选：下载 artifact（`openssl-linux-x64`，保留 7 天）：
   `gh run download <run-id> -n openssl-linux-x64 -D third_party/openssl-linux-x64`。
3. 确认 `include/openssl/ssl.h`、`lib/libssl.a`、`lib/libcrypto.a` 齐备后随仓库提交。

构建选项与产物约定：目标 `linux-x86_64`，`-fPIC`（位置无关代码，与 PIE 主程序一致）、`no-shared` / `no-tests` / `no-apps` / `no-zlib`（与 Windows/macOS 构建一致）、`--libdir=lib`（统一 `lib/` 安装布局），在 `ubuntu-24.04` runner 上产出。

---

# Third-Party Dependencies (English)

[简体中文](#third_party-三方依赖清单) | **English**

This directory holds all third-party dependencies of TuPigGHD2Chinese. **Everything is committed to the repository** (including the Windows, macOS and Linux OpenSSL static libraries), so a clone can be built without downloading any dependencies.

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
- **OpenSSL**: follow **LTS** branches only; the static libraries on all three platforms (Windows / macOS / Linux) are produced by the main workflow's vendor jobs. After a successful vendor run, the **OpensslVendorPR** job assembles `third_party` and **opens an intake PR automatically** (validation and flow below), which a maintainer reviews and merges via squash; **the produced version is enforced to be the latest official Release of openssl/openssl** — anything older is rejected before a PR is opened. The maintainer's local Windows build steps below remain as a fallback. After the upgrade is merged, update in sync: the version table in this file, `README.md`, `SECURITY.md`, and `docs/wiki/编译指南.md`.

## Automatic OpenSSL Vendor Intake Flow

Trigger: the `vendor-openssl` label on a PR, or a manual run of the main workflow with `type=vendor` (an empty `openssl-version` input resolves to the latest official Release; not locked to 3.x).

1. Once all four vendor jobs (Windows / Linux / macOS x64 / macOS arm64) succeed, the **OpensslVendorPR** job automatically:
   - verifies the produced version is the **latest official Release** of openssl/openssl (GitHub API `releases/latest`; anything older aborts before a PR is opened);
   - verifies the four platform source tarball SHA256 digests are identical (each downloaded independently, independently provenanced);
   - verifies the artifact version macro (`OPENSSL_VERSION_STR`) matches the produced version and that `libssl` / `libcrypto` are present, then assembles the four `third_party` directories;
   - skips automatically when the version matches what is already in the repository (no duplicate PR); reruns of the same run are guarded by branch-existence checks.
2. Opens the intake PR automatically (branch `vendor/openssl-<version>-r<run-id>`, including the four-platform provenance SHA256 table and a maintainer review checklist). Note: PRs created with `GITHUB_TOKEN` **do not trigger CI** (GitHub recursion guard), so review relies on the maintainer validating locally per the checklist.
3. Maintainer review checklist (see the PR description): all four vendor jobs green, SHA256 cross-checked against openssl.org, local build + `version_test` + smoke tests pass, documentation versions synced — then **squash merge** into the repository.

## Windows OpenSSL Static Library Build Steps (maintainer fallback reference)

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

## macOS OpenSSL Static Library Generation Steps (maintainer fallback reference)

Both macOS architectures are produced by the **产库 OpenSSL (macOS)** jobs of the main workflow [.github/workflows/tupigghd2chinese.yml](../.github/workflows/tupigghd2chinese.yml) (triggered by the `vendor-openssl` label on a PR or manual `type=vendor`, not part of day-to-day CI):

1. Add the `vendor-openssl` label to a PR (labeled event), or run the main workflow manually from the GitHub Actions page with `type=vendor`; an empty `openssl-version` input resolves to the latest official Release.
2. Once the run completes, the artifacts go through the **automatic vendor intake flow** (see above) to assemble the PR; manual fallback: download the two artifacts (`openssl-macos-x64` / `openssl-macos-arm64`, retained for 7 days):
   `gh run download <run-id> -n openssl-macos-x64 -D third_party/openssl-macos-x64` (likewise for arm64).
3. Verify that `include/openssl/ssl.h`, `lib/libssl.a` and `lib/libcrypto.a` are present, then commit them to the repository.

Build options and artifact conventions (identical to the previous inline build in the main workflow):

- Target `darwin64-x86_64-cc` produced on the `macos-15-intel` runner; `darwin64-arm64-cc` produced on the `macos-15` runner (native Apple Silicon) — same toolchain as the CI builds
- `no-shared` (static libraries `libssl.a` / `libcrypto.a`), `no-tests` / `no-apps`, `no-zlib` (consistent with the Windows build; avoids introducing a zlib dependency)
- `MACOSX_DEPLOYMENT_TARGET=11.0`: arm64 artifacts support macOS 11 at minimum, unifying the deployment target across both architectures

## Linux OpenSSL Static Library Generation Steps (maintainer fallback reference)

The Linux x64 static library is produced by the **产库 OpenSSL (linux x64)** job of the main workflow (triggered the same way as macOS, see the section above):

1. Add the `vendor-openssl` label to a PR, or run the main workflow manually with `type=vendor`.
2. Once the run completes, the artifacts go through the **automatic vendor intake flow** (see above) to assemble the PR; manual fallback: download the artifact (`openssl-linux-x64`, retained for 7 days):
   `gh run download <run-id> -n openssl-linux-x64 -D third_party/openssl-linux-x64`.
3. Verify that `include/openssl/ssl.h`, `lib/libssl.a` and `lib/libcrypto.a` are present, then commit them to the repository.

Build options and artifact conventions: target `linux-x86_64`, `-fPIC` (position-independent code, matching the PIE main program), `no-shared` / `no-tests` / `no-apps` / `no-zlib` (consistent with the Windows/macOS builds), `--libdir=lib` (unified `lib/` install layout), produced on the `ubuntu-24.04` runner.
