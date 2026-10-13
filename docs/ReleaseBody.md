### 1.2.30 起的修复与更新 / Fixes & Updates since 1.2.30

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

<!--
#### 修复BUG:
- 

#### 更新内容:
- 

-->


### 程序说明 / Program Notes  
1. 可以仅下载二进制程序,双击运行后自动汉化  
   You can simply download the binary and double-click to run it — the UI is localized automatically.
2. 如果运行提示出错,可重试或下载[此JSON文件](https://github.com/Tupig/GitHubDesktop2Chinese/blob/main/json/localization.json)后放在同目录后运行二进制程序  
   If the program reports an error, retry, or download [this JSON file](https://github.com/Tupig/GitHubDesktop2Chinese/blob/main/json/localization.json), put it in the same directory as the binary and run again.
3. **若使用GitHub 仓库作为json文件源，请升级加载器到最新版本**  
   **If you use the GitHub repository as the JSON file source, please upgrade the loader to the latest version.**
4. `GitHubDesktop2Chinese.exe` 为 64 位 Windows 程序，不再提供 32 位版本；Release 页同时提供 macOS 分架构二进制（`macos-x64` / `macos-arm64`）与 Linux x64 版本  
   `GitHubDesktop2Chinese.exe` is a 64-bit Windows program; 32-bit builds are no longer provided. The Release page also ships per-architecture macOS binaries (`macos-x64` / `macos-arm64`) and a Linux x64 build.
5. 如果汉化后主程序无法打开，请更新加载器后执行参数 `GitHubDesktop2Chinese.exe dev --translationfrombak`
   If GitHub Desktop fails to open after localization, update the loader and run it with `GitHubDesktop2Chinese.exe dev --translationfrombak`.
