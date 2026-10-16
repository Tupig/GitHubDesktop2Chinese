### 修复与更新 / Fixes & Updates

#### 版本更新日志 / Changelog:
- 版本策略重置: 自 v1.2.30 起仅维护最新版本, 历史变更条目不再保留; 映射 `minversion` 提升至 1.2.30, 旧版加载器加载新映射时将提示需升级（v1.2.30）
  - Version policy reset: only the latest version is maintained from v1.2.30 on and historical changelog entries are no longer kept; the mapping `minversion` is raised to 1.2.30 — older loaders loading newer mappings will be prompted to upgrade（v1.2.30）
- 审计修复: 工具解压完整性检查改为尾部 64KB 搜索 EOCD(兼容含注释归档)、检测工具新增 ReDoS 嵌套量词告警(与 CI 对齐)、网络响应体上限 32MB、解压应用目录查找迭代化（v1.2.30）
  - Audit fixes: archive integrity checking now searches for the EOCD in the trailing 64KB (tolerates archives with comments); the detection tool gained a ReDoS nested-quantifier warning (aligned with CI); network response bodies are capped at 32MB; the app-directory lookup during extraction is now iterative（v1.2.30）
- 安全与健壮性审计修复: 网络下载与自更新、输入交互、版本解析、系统环境探测等环节共整改 18 项(高危 3 / 中危 5 / 低危 10)（v1.2.31）
  - Security & robustness audit fixes: 18 issues remediated across network download & self-update, interactive input, version parsing and system-environment probing (3 high / 5 medium / 10 low)（v1.2.31）
- 依赖更新: 随仓预编译 OpenSSL 由 3.3.0(已停止维护) 升级至 3.5.9 LTS, Windows 构建不再联网下载依赖(CMake 配置完全离线)（v1.2.32）
  - Dependency update: the prebuilt OpenSSL shipped in the repo was upgraded from 3.3.0 (EOL) to 3.5.9 LTS; Windows builds no longer download dependencies (CMake configuration is fully offline)（v1.2.32）
- CI 增强: 工具自检新增 Markdown 链接检查; 新增本地冒烟测试脚本 tools/ci/smoke-test.ps1 与三方组件版本清单 third_party/README.md（v1.2.32）
  - CI enhancements: the tool self-test now includes a Markdown link check; added the local smoke-test script tools/ci/smoke-test.ps1 and the third-party component inventory third_party/README.md（v1.2.32）
- 工程规范: 文本编码统一为 UTF-8 无 BOM 形态(CMake 配置 / JSON / Markdown / C++ 头文件共 6 处去除 BOM), 冒烟测试脚本补 UTF-8 BOM 兼容本地 Windows PowerShell 5.1 运行（v1.2.33）
  - Engineering standards: text encoding unified to UTF-8 without BOM (BOM removed from 6 files across CMake config / JSON / Markdown / C++ headers); the smoke-test script gained a UTF-8 BOM so it runs on local Windows PowerShell 5.1（v1.2.33）
- 构建治理: 移除弃用的 codecvt 宽窄转换(消除 macOS 构建告警), 第三方头文件改为 SYSTEM 包含并屏蔽 MSVC LNK4099(第三方库无 PDB), 清理 CMake 残留调试注释, 目标四平台构建零告警（v1.2.34）
  - Build hygiene: removed the deprecated codecvt wide-narrow conversion (clears the macOS build warning); third-party headers are now included as SYSTEM and MSVC LNK4099 (third-party libs ship no PDBs) is suppressed; a leftover CMake debug comment was cleaned up — aiming for zero build warnings on all four platforms（v1.2.34）
- 代码治理: 远程端点与魔法数字集中常量化, 消除散落各调用点的硬编码 URL/数值(GitHub API/RAW 端点、EOCD 搜索窗口、UI 文案长度阈值等), 调整端点或阈值只需改一处 (#94)（v1.2.35）
  - Code governance: remote endpoints and magic numbers centralized into named constants — scattered hardcoded URLs/values (GitHub API/RAW endpoints, the EOCD search window, UI-text length thresholds, etc.) are now managed in one place, so adjusting an endpoint or threshold takes a single edit (#94)（v1.2.35）
- 依赖统一: macOS 切换为仓库内置 OpenSSL 3.5.9 静态库(双架构随仓库提交, 由 vendor 工作流产出), 构建期不再依赖 Homebrew 探测、源码编译与 Actions 缓存, 三平台统一为“克隆即可离线构建” (#98)（v1.2.36）
  - Dependency unification: macOS now builds against OpenSSL 3.5.9 static libraries bundled in the repo (both architectures committed, produced by the vendor workflow) — builds no longer depend on Homebrew probing, source compilation or Actions caches; all three platforms are now “clone and build offline” (#98)（v1.2.36）
- 版本体系重置: 版本号自 v0.0.1 重新计数, 旧 v1.2.x 的 Release 与 tag 已全部清理; 递增规则改为 patch 满 100 进位 minor 并重置为 1、minor 满 100 进位 major 并重置为 0（0.0.100 → 0.1.1, 0.100.100 → 1.0.1）; 映射 `minversion` 随加载器版本同步调整（v0.0.1）
  - Version scheme reset: version numbers restart from v0.0.1 — the old v1.2.x releases and tags have all been removed; the increment rule now carries a full patch (100) into minor resetting to 1, and a full minor (100) into major resetting to 0 (0.0.100 → 0.1.1, 0.100.100 → 1.0.1); the mapping `minversion` follows the loader version accordingly（v0.0.1）
- 隐私修复: 随仓 Windows OpenSSL 静态库改为 CI 环境构建并重新提交, 消除库文件与发布产物中内嵌的本地构建路径信息（v0.0.1）
  - Privacy fix: the bundled Windows OpenSSL static libraries are rebuilt in CI and re-committed, removing local build-path information embedded in the libraries and release artifacts（v0.0.1）
- 依赖统一(补全): Linux 同步切换为仓库内置 OpenSSL 3.5.9 静态库(x64, -fPIC, 由主工作流 vendor 任务产出并随仓库提交), 构建不再依赖 libssl-dev, 三平台全部统一为“克隆即可离线构建” (#98)（v0.0.1）
  - Dependency unification (completed): Linux now also builds against the OpenSSL 3.5.9 static library bundled in the repo (x64, -fPIC, produced by the main workflow's vendor jobs and committed) instead of libssl-dev — all three platforms are now uniformly “clone and build offline” (#98)（v0.0.1）
- CI 单工作流化: wiki 同步与 OpenSSL 产库任务并入主工作流, 删除独立工作流文件; 新增单工作流规范校验器(连通性/无循环/无孤立任务等)与 20 用例测试防回归 (#98)（v0.0.1）
  - Single-workflow CI: the wiki-sync and OpenSSL vendor jobs are merged into the main workflow and the standalone workflow files are removed; a single-workflow lint (connectivity / no cycles / no orphan jobs) plus 20 test cases guard against regressions (#98)（v0.0.1）
- 版本重发: v0.0.2 与 v0.0.1 指向同一提交, 无代码变更（v0.0.2）
  - Version re-publish: v0.0.2 points to the same commit as v0.0.1, with no code changes（v0.0.2）
- 文档治理: 行尾策略锁定与维护备忘落地 (#115)（v0.0.3）
  - Documentation: line-ending policy locked in with maintenance memos landed (#115)（v0.0.3）
- 文档修正: 修正依赖策略与 README 过时表述 (#116)（v0.0.3）
  - Documentation fix: corrected outdated dependency-policy and README wording (#116)（v0.0.3）
- 审计修复: workflow 权限最小化、下载完整性校验与工具链一致性 (#124)（v0.0.3）
  - Audit fixes: workflow permission minimization, download-integrity verification and toolchain consistency (#124)（v0.0.3）
- 修复: 「关于」菜单中的汉化插件链接改指本仓库 (#126)（v0.0.3）
  - Fix: the localized About-menu plugin link now points to this repository (#126)（v0.0.3）
- 关键缺陷修复: 版本解析与退出码关键缺陷(A1/A2/D1)修复, 新增 C++ 单元测试基础设施 version_test (#127)（v0.0.4）
  - Critical fixes: key version-parsing and exit-code defects (A1/A2/D1) fixed; C++ unit-test infrastructure (version_test) added (#127)（v0.0.4）
- 项目品牌化（更名说明）: 项目与仓库更名为 TuPigGHD2Chinese, 发布资产名由 GitHubDesktop2Chinese.* 改为 TuPigGHD2Chinese.*; ⚠️ 自更新按资产名精确匹配, 旧版本无法自动升级到本版本, 请手动下载新版; 旧版本远程取映射依赖 GitHub 对旧仓库名的重定向兜底（旧名被占用则失效）（v0.1.12）
  - Rebranding (rename notice): the project and repository are renamed to TuPigGHD2Chinese, and release asset names change from GitHubDesktop2Chinese.* to TuPigGHD2Chinese.*; ⚠️ self-update matches asset names exactly, so older versions cannot auto-update to this build — please download the new build manually; older versions rely on GitHub's redirect of the old repository name for remote mappings (which stops working if the old name is taken)（v0.1.12）
- 测试框架现代化: C++ 单元测试迁移至 GoogleTest（随仓 vendored, 构建期不下载）, Python 校验器测试迁移至 pytest, Node 工具测试迁移至 Vitest; CI 质量门覆盖全部校验器测试（v0.1.12）
  - Test-framework modernization: C++ unit tests moved to GoogleTest (vendored in-repo; nothing downloaded at build time), Python validator tests moved to pytest, and Node tool tests moved to Vitest; the CI quality gate now runs every validator test（v0.1.12）

<!--
#### 修复BUG:
- 

#### 更新内容:
- 

-->


### 程序说明 / Program Notes  
1. 可以仅下载二进制程序,双击运行后自动汉化  
   You can simply download the binary and double-click to run it — the UI is localized automatically.
2. 如果运行提示出错,可重试或下载[此JSON文件](https://github.com/Tupig/TuPigGHD2Chinese/blob/main/json/localization.json)后放在同目录后运行二进制程序  
   If the program reports an error, retry, or download [this JSON file](https://github.com/Tupig/TuPigGHD2Chinese/blob/main/json/localization.json), put it in the same directory as the binary and run again.
3. **若使用GitHub 仓库作为json文件源，请升级加载器到最新版本**  
   **If you use the GitHub repository as the JSON file source, please upgrade the loader to the latest version.**
4. `TuPigGHD2Chinese.exe` 为 64 位 Windows 程序，不再提供 32 位版本；Release 页同时提供 macOS 分架构二进制（`macos-x64` / `macos-arm64`）与 Linux x64 版本  
   `TuPigGHD2Chinese.exe` is a 64-bit Windows program; 32-bit builds are no longer provided. The Release page also ships per-architecture macOS binaries (`macos-x64` / `macos-arm64`) and a Linux x64 build.
5. 如果汉化后主程序无法打开，请更新加载器后执行参数 `TuPigGHD2Chinese.exe dev --translationfrombak`
   If GitHub Desktop fails to open after localization, update the loader and run it with `TuPigGHD2Chinese.exe dev --translationfrombak`.
