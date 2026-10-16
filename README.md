# TuPigGHD2Chinese

[![CI](https://img.shields.io/github/actions/workflow/status/Tupig/TuPigGHD2Chinese/tupigghd2chinese.yml?branch=main&label=CI)](https://github.com/Tupig/TuPigGHD2Chinese/actions/workflows/tupigghd2chinese.yml)
[![Release](https://img.shields.io/github/v/release/Tupig/TuPigGHD2Chinese)](https://github.com/Tupig/TuPigGHD2Chinese/releases)
[![License](https://img.shields.io/github/license/Tupig/TuPigGHD2Chinese)](LICENSE.txt)
[![C++](https://img.shields.io/badge/C%2B%2B-20-blue?logo=cplusplus&logoColor=white)](#-怎么编译源代码)
[![localization.json](https://img.shields.io/badge/localization.json-v3-green)](#-映射文件-localizationjson)

**简体中文** | [English](#tupigghd2chinese-english)

> 本仓库派生（fork）自 [cngege/GitHubDesktop2Chinese](https://github.com/cngege/GitHubDesktop2Chinese)，在此基础上继续维护与更新。原作者 [CNGEGE](https://github.com/cngege)，感谢其开创性工作。

> [!TIP]
> 📚 **更详细的文档**见 [docs/wiki 文档中心](docs/wiki/Home.md)：[安装与使用](docs/wiki/安装与使用.md) · [命令行参数](docs/wiki/命令行参数.md) · [常见问题](docs/wiki/常见问题.md) · [汉化原理](docs/wiki/汉化原理.md) · [映射文件格式](docs/wiki/映射文件格式.md) · [正则表达式指南](docs/wiki/正则表达式指南.md) · [编译指南](docs/wiki/编译指南.md) · [CI/CD](docs/wiki/CI-CD.md) · [贡献指南](docs/wiki/贡献指南.md) · [自动维护工具](docs/wiki/自动维护工具.md)

## 目录

- [🥮 这是什么](#-这是什么)
- [🎯 怎么使用它](#-怎么使用它)
- [⚙️ 命令行参数](#-命令行参数)
- [🏗 怎么编译源代码](#-怎么编译源代码)
- [👕 怎么贡献汉化](#-怎么贡献汉化)
- [🍬 映射文件 localization.json](#-映射文件-localizationjson)
- [🧪 CI / 自动维护](#-ci--自动维护)
- [📁 项目结构](#-项目结构)
- [🔭 开启 GitHub Desktop 预览版选项](#-开启-github-desktop-预览版选项)
- [🧭 常见问题](#-常见问题)
- [🎋 功能特性](#-功能特性)
- [📦 第三方库](#-第三方库)
- [⭐ 星标历史](#-星标历史)
- [🏘 感谢大家的群策群力](#-感谢大家的群策群力)

## 🥮 这是什么

一个自动替换 [GitHub Desktop](https://desktop.github.com/)（[官方仓库 desktop/desktop](https://github.com/desktop/desktop)）界面文本为目标语言（中文）的程序：

- **高兼容性**：采用正则映射替换，对 GitHub Desktop 频繁更新的版本变化兼容性高
- **低维护成本**：版本更新后仅需手动补充个别失效翻译条目
- **可自动更新**：加载器自动检测新版本，支持一键更新与断点续传

## 🎯 怎么使用它

[🎀 视频教程（BiliBili）](https://www.bilibili.com/video/BV17HpSeHEaC/) · [📖 详细教程：安装与使用](docs/wiki/安装与使用.md)

**方式一（推荐）**：前往 [Releases](https://github.com/Tupig/TuPigGHD2Chinese/releases) 下载 `TuPigGHD2Chinese.exe`，双击运行，程序自动联网获取最新 `localization.json` 完成汉化。

**方式二**：下载 `TuPigGHD2Chinese.exe` 与 `localization.json`，放在同一文件夹后运行。

> [!IMPORTANT]
> - 本程序提供 **64 位 Windows / macOS（按架构分 x64 与 arm64 两个包）/ Linux（x64）** 多平台版本，从 [Releases](https://github.com/Tupig/TuPigGHD2Chinese/releases) 页选择对应产物下载。
> - 发布产物附带构建溯源证明（SLSA provenance），可用 `gh attestation verify <下载的文件> --repo Tupig/TuPigGHD2Chinese` 校验来源；Windows 产物的代码签名流程已内置（SignPath 集成），凭据配置后自动启用，当前产物未签名。
> - 汉化映射以 **Windows 版** GitHub Desktop 文案为准，其他平台文案基本一致，个别条目可能不匹配。
> - GitHub Desktop 每次版本更新后，都需要重新运行一次本程序才能完成汉化。

## ⚙️ 命令行参数

> 所有参数均为可选；不带参数双击运行即走默认流程（自动查找 GitHub Desktop 目录 + 联网获取最新映射）。
>
> **参数顺序**：`--nopause` 是程序级参数，必须写在子命令（如 `dev`）**之前**，例如 `TuPigGHD2Chinese.exe --nopause dev --invalidcheck`。

### 通用参数

| 参数 | 说明 |
| --- | --- |
| `-h, --help` | 显示帮助并退出 |
| `-g, --githubdesktoppath <目录>` | 指定 GitHub Desktop 的 js 所在目录（须含 `index.html`、`main.js`、`renderer.js`） |
| `-j, --json <路径>` | 指定本地 `localization.json`（须以 `.json` 结尾；文件不存在时会自动创建模板并提示退出） |
| `-r, --onlyfromremote` | 仅从远程仓库读取映射文件（忽略本地） |
| `--rollback` | 从 `main.js.bak` / `renderer.js.bak` 还原到汉化前状态 |
| `--nopause` | 结束前不再暂停等待（脚本/自动化场景；版本不满足时不再询问、直接强制替换） |

### 开发者参数（`dev` 子命令）

| 参数 | 说明 |
| --- | --- |
| `-d, --dev` | 启动时直接进入开发者选项菜单（等同按住 `Shift` 启动） |
| `--mainerrorcheck` | [错误检查模式] 对 `main.js` 逐条排查 |
| `--rendererrorcheck` | [错误检查模式] 对 `renderer.js` 逐条排查 |
| `--invalidcheck` | 检测 `localization.json` 中失效（匹配不到/正则非法）的条目 |
| `--noreplaceres` | 不替换资源（不影响错误检查模式） |
| `--translationfrombak` | 优先从 `.bak` 备份读取 js 内容进行替换（建议配合 `--invalidcheck`） |
| `--devreplace` | 仅替换 `main_dev` / `renderer_dev` 中的映射，快速调试 |
| `--devsetver` | 指定当前程序版本（仅开发者版本 `0.0.0-Dev` 可用） |

### 常用示例

```powershell
# 常规汉化（自动查找目录 + 联网获取最新映射）
TuPigGHD2Chinese.exe

# 指定 GitHub Desktop 目录与本地映射文件
TuPigGHD2Chinese.exe -g "$env:LOCALAPPDATA\GitHubDesktop\app-<版本>\resources\app" -j .\localization.json

# 仅使用远程映射（忽略本地同名文件）
TuPigGHD2Chinese.exe -r

# 批量检测失效条目（免交互：--nopause 必须在 dev 之前）
TuPigGHD2Chinese.exe --nopause dev --invalidcheck

# 汉化后主程序无法打开：从备份恢复
TuPigGHD2Chinese.exe dev --translationfrombak

# 撤销汉化、还原汉化前状态
TuPigGHD2Chinese.exe --rollback
```

> [!TIP]
> 查看内置帮助：`TuPigGHD2Chinese.exe --help`。完整参数说明（含开发者菜单、退出码、错误检查模式详解）见 [📖 Wiki：命令行参数](docs/wiki/命令行参数.md)。

## 🏗 怎么编译源代码

> 项目基于 CMake，支持 **MSVC（Windows）/ AppleClang（macOS）/ GCC（Linux）** 三套工具链，仅支持 64 位（x64 / arm64）构建，其他架构会在 CMake 阶段直接报错；**所有发布产物均为 64 位**。各平台环境准备与常见构建错误见 [📖 Wiki：编译指南](docs/wiki/编译指南.md)。

1. 克隆仓库
2. 使用 **VS2022** 直接打开项目文件夹（通过 CMake 打开）
3. 选择 `x64-debug` / `x64-release` 预设进行构建（预设面向 Windows MSVC/Ninja；macOS/Linux 用下方命令行）

命令行构建（Windows / macOS / Linux 通用）：

```powershell
cmake -B build
cmake --build build --config Release
```

> [!NOTE]
> OpenSSL 依赖：三平台静态库与头文件均随仓库提交（Windows `third_party/openssl/`、macOS `third_party/openssl-macos-{x64,arm64}/`、Linux `third_party/openssl-linux-x64/`，无需预装或联网下载；版本与更新方式见 [`third_party/README.md`](third_party/README.md)）。CI 发布的 **macOS 产物按架构拆分（`macos-x64` / `macos-arm64`），各平台静态链接 OpenSSL，macOS 产物另经 strip/LTO 裁剪**，自包含无额外依赖。

## 👕 怎么贡献汉化

1. 克隆仓库，阅读 [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md)（更详细的流程见 [📖 Wiki：贡献指南](docs/wiki/贡献指南.md)，正则写法见 [📖 Wiki：正则表达式指南](docs/wiki/正则表达式指南.md)）
2. 在 [`json/localization.json`](json/localization.json) 中参照已有格式补充翻译条目
3. 提交 PR

**开发调试技巧**：新写的条目可先放入 `main_dev` 或 `renderer_dev`，然后按住 `Shift` 启动程序，开启「仅替换指定映射项」进行快速测试；完成后按 [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md) 的条目顺序规则移动到 `main` / `renderer`（一般追加到末尾；若与更宽泛条目存在「短含长」，须插到其前面）再提交 PR。

## 🍬 映射文件 localization.json

存储 GitHub Desktop 英文文本到中文文本的映射，通过正则匹配完成替换。项目日常更新主要维护此文件（字段与条目规则详见 [📖 Wiki：映射文件格式](docs/wiki/映射文件格式.md)，正则写法见 [📖 Wiki：正则表达式指南](docs/wiki/正则表达式指南.md)）。

- 路径：`json/localization.json`

| 节点 | 类型 | 说明 |
| --- | --- | --- |
| `version` | int | JSON 文件格式版本，仅格式变更时递增 |
| `minversion` | string | 需要的最低加载器版本 |
| `tip` | string[] | 加载器中显示的通知信息 |
| `select` | object[] | 本地化时提示用户选择性修改的条目 |
| `main` | array | 用于替换 `main.js` 的映射 |
| `main_dev` | array | `main.js` 开发调试用快速替换映射 |
| `renderer` | array | 用于替换 `renderer.js` 的映射 |
| `renderer_dev` | array | `renderer.js` 开发调试用快速替换映射 |

`select` 数组元素结构：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `replaceFile` | string | 目标文件（`main.js` / `renderer.js`） |
| `tooltip` | string | 提示信息 |
| `enable` | bool | 此条是否启用 |
| `replace` | string[][] | 二维替换数组，`[查找正则, 替换文本, 可选查找正则]` |

## 🧪 CI / 自动维护

本仓库使用统一工作流 [`tupigghd2chinese.yml`](.github/workflows/tupigghd2chinese.yml)（job 矩阵、发布规则与质量门明细见 [📖 Wiki：CI/CD](docs/wiki/CI-CD.md)）：

| 功能 | 触发方式 | 说明 |
| --- | --- | --- |
| 构建 | push `main` / PR（目标 main）/ tag `v*` / 定时 / 手动 auto、build、release | 构建矩阵 + 产物功能测试：Windows x64、macOS x64 / arm64（单架构包）、Linux x64 |
| JSON 质量校验 | PR（目标 main）/ push `main` / tag `v*` / 定时 / 手动 auto、security、release | 正则合法性、结构完整性、占位符检查、std::regex 不兼容语法黑名单、译文问句全角风格 |
| 工具自检 | PR（目标 main）/ push `main` / tag `v*` / 定时 / 手动 auto、security、maintain、release | 自动维护工具语法检查 + 单元测试 + Markdown 链接检查 |
| CodeQL 扫描 | PR（目标 main）/ push `main` / 手动 auto、security、release | C/C++ 安全扫描 |
| 失效检测 + 候选提取 | push `main` / 定时（每日）/ 手动 auto、maintain、release | 检测失效映射、提取未翻译候选，自动创建/关闭 Issue |
| Release 发布 | push `main`（有变更时）/ tag `v*` / 定时（有变更时）/ 手动 auto（仅 main, 有变更）、release（仅 main） | 自动升级版本号，发布多平台产物 + localization.json |

> 手动触发的 `type` 仅执行对应子集；`version` 为可选覆盖——**留空即自动递增版本**（如 `0.0.1 → 0.0.2`，patch 满 100 进位 minor），填写时须为 `x.y.z` 纯数字，否则 CI 在版本号校验处直接失败。

> [!TIP]
> 手动触发 `type=auto` 会跑完整链路（构建 → 检查 → 维护 → 发布），版本号自动递增（如 `0.0.1 → 0.0.2`）；也可在 `version` 输入框手动指定（`x.y.z` 格式）。

自动维护工具位于 [`tools/auto-maintain`](tools/auto-maintain)，基于 **Windows 版** GitHub Desktop 的 `main.js` / `renderer.js` 检测；本地运行：

```powershell
cd tools/auto-maintain
npm run all        # 失效检测 + 未翻译候选提取, 报告写入系统临时目录 ghdesktop-auto-maintain/report.md
```

> [!NOTE]
> **定时与发布规则**
> - **定时调度**：每日北京时间 10:00（UTC 02:00）自动执行构建与自动维护。
> - **变更检测**：以最新 `v*` tag 为基准，检测 `json/`、`src/`、`third_party/`、`CMakeLists.txt`、`CMakePresets.json` 的实际变更。
> - **条件发布**：push `main`、定时与手动 `auto` 仅在检测到上述变更时发布新版本（补丁号 +1）；无变更自动跳过，避免空版本。`type=release` 与推送 tag 始终发布。

## 📁 项目结构

架构概览：一个 C++ 主程序（`src/`，加载器 + 汉化器，产物为单文件 exe）、一份汉化映射数据（`json/localization.json`，运行时从本地或远程加载）、一个 Node.js 自动维护工具（`tools/auto-maintain`）以及统一 CI 工作流（`.github/workflows`，CI 脚本位于 `tools/ci`）。

```text
.
├── src/                          # C++ 主程序源码
│   ├── TuPigGHD2Chinese.cpp # 程序入口与汉化主流程
│   ├── TuPigGHD2Chinese.h
│   ├── utils/                    # 通用工具（伞形入口 + 按职责拆分的子头）
│   │   ├── utils.hpp             # 伞形汇总头（对外唯一入口）
│   │   ├── encoding.hpp          # 编码转换（宽窄字符串 / 路径构造）
│   │   ├── system.hpp            # 环境变量与系统代理探测
│   │   ├── fs_io.hpp             # 文件读写
│   │   ├── http.hpp              # HTTP / TLS 校验 / 自动更新
│   │   └── input.hpp             # 交互输入
│   └── version/Version.hpp       # 版本号解析（自研代码）
├── third_party/                  # 第三方依赖（随仓库提交，含预编译 OpenSSL 三平台静态库）
│   ├── include/                  # CLI11、cpp-httplib、nlohmann/json、spdlog、WinReg
│   ├── openssl/                  # OpenSSL 3.5.9 头文件与 Windows 预编译静态库
│   ├── openssl-macos-{x64,arm64}/ # OpenSSL 3.5.9 macOS 预编译静态库（按架构）
│   ├── openssl-linux-x64/        # OpenSSL 3.5.9 Linux 预编译静态库（-fPIC）
│   └── README.md                 # 三方组件版本清单与本地构建/更新说明
├── json/
│   └── localization.json         # 汉化映射（核心数据；路径为已发布程序的公共契约，不可移动）
├── tools/
│   ├── auto-maintain/            # localization.json 自动维护工具（失效检测 / 候选提取）
│   └── ci/                       # CI 脚本（数据质量校验 / 工具自检 / Markdown 链接检查 / 冒烟测试 / 维护报告）
├── docs/                         # 文档
│   ├── README.md                 # 文档索引与归类说明
│   ├── CONTRIBUTING.md           # 汉化贡献指南（GitHub 自动识别）
│   ├── ReleaseBody.md            # Release 说明模板（CI 拼接进发布说明）
│   └── wiki/                     # 详细 wiki 文档（使用 / 原理 / 贡献 / 开发）
├── .github/
│   └── workflows/                # 统一 CI/CD 工作流（仓库唯一工作流）
├── SECURITY.md                   # 安全政策（私密漏洞报告 / 供应链说明）
├── CMakeLists.txt
└── CMakePresets.json
```

## 🔭 开启 GitHub Desktop 预览版选项

GitHub Desktop 内部预览版判断机制：

```javascript
const nn = !1;
function rn() {
    return !nn && "1" === process.env.GITHUB_DESKTOP_PREVIEW_FEATURES
}
// rn() 返回 true 时，开启预览版机制
```

**方式一**：设置环境变量后启动 GitHub Desktop

```cmd
set GITHUB_DESKTOP_PREVIEW_FEATURES=1
"GitHub Desktop.lnk"
```

**方式二**：通过加载器运行时按提示选择，自动开启预览版功能（对应 `select` 中的可选替换项）。

## 🧭 常见问题

> [!TIP]
> **找不到 openssl 的 DLL**：请更新到 [最新版本](https://github.com/Tupig/TuPigGHD2Chinese/releases)。
>
> **提示「文件要求加载器版本至少为…」/ 版本不满足**：远程映射仅适配最新版加载器，请到 [Releases](https://github.com/Tupig/TuPigGHD2Chinese/releases) 升级加载器；也可在提示时输入 `f` 强制替换（不建议，可能出现条目不匹配）。
>
> **程序不运行 / 一闪而过 / 缺失 `MSVCP140_ATOMIC_WAIT.dll`**：安装 [Microsoft Visual C++ Redistributable](https://learn.microsoft.com/zh-cn/cpp/windows/latest-supported-vc-redist?view=msvc-170) 的 **x64** 版本（`vc_redist.x64.exe`）。
>
> 安装最新 VC++ 运行库后仍无法运行时，请检查程序目录下是否残留 `MSVCP140.dll`、`VCRUNTIME140.dll` 等文件，如有请删除。
>
> **汉化后主程序无法打开**：更新加载器后执行 `TuPigGHD2Chinese.exe dev --translationfrombak`。
>
> **想撤销汉化、还原为汉化前状态**：执行 `TuPigGHD2Chinese.exe --rollback`（从 `main.js.bak` / `renderer.js.bak` 还原）。
>
> **是否支持 macOS / Linux？**：提供 macOS 分架构二进制（Intel 下载 `macos-x64`、Apple Silicon 下载 `macos-arm64`）与 Linux x64 版本（Release 页下载）。汉化映射针对 **Windows 版** GitHub Desktop 的界面文案（含 `&` 访问键等 Windows 专属内容），其他平台个别条目可能不匹配。
>
> **脚本 / 自动化运行**：加 `--nopause` 免交互；注意它是程序级参数，须写在 `dev` 等子命令之前。
>
> 更多问题（代理不生效、`#{n}` 占位符、断点续传、`--invalidcheck` 等）见 [📖 Wiki：常见问题](docs/wiki/常见问题.md)。

有任何建议欢迎提 [Issues](https://github.com/Tupig/TuPigGHD2Chinese/issues)。

安全漏洞请按 [`SECURITY.md`](SECURITY.md) 私密报告；发布产物可用 `gh attestation verify <文件> --repo Tupig/TuPigGHD2Chinese` 校验构建来源。

## 🎋 功能特性

- [x] JSON 格式标识文件版本与最低加载器版本
- [x] 加载器程序版本宏定义
- [x] 替换映射第三项（查找参数）全局正则查找，`#{number}` 占位符回填
- [x] 最低版本校验，不满足时提示或询问是否强制替换
- [x] 暂停确认（结束前等待按键、`minversion` 不满足时输入 `f` 才强制替换）——`--nopause` 一并跳过（注意：版本不满足时将不再询问、直接强制替换）
- [x] 自动检测更新，一键更新 + 断点续传
- [x] JSON 附加描述文本（`tip`）在加载器中显示
- [x] 汉化完成后显示项目参与者
- [x] 汉化异常后从备份恢复
- [x] 选择性汉化（`select` 提示项）
- [x] 预览版功能一键开启
- [x] 系统 HTTP 代理支持（环境变量 + 注册表）
- [x] 读取 GitHub Desktop 最新版与本地版本对比提示

## 📦 第三方库

感谢以下优质开源项目：

| 库 | 用途 | 仓库 |
| --- | --- | --- |
| CLI11 | 命令行参数解析 | [CLIUtils/CLI11](https://github.com/CLIUtils/CLI11) |
| cpp-httplib | HTTP 客户端 | [yhirose/cpp-httplib](https://github.com/yhirose/cpp-httplib) |
| nlohmann/json | JSON 解析 | [nlohmann/json](https://github.com/nlohmann/json) |
| spdlog | 日志输出 | [gabime/spdlog](https://github.com/gabime/spdlog) |
| WinReg | Windows 注册表操作 | [GiovanniDicanio/WinReg](https://github.com/GiovanniDicanio/WinReg) |
| OpenSSL | SSL/TLS 支持 | [openssl/openssl](https://github.com/openssl/openssl) |

## ⭐ 星标历史

[![Star History Chart](https://api.star-history.com/svg?repos=Tupig/TuPigGHD2Chinese&type=Date)](https://star-history.com/#Tupig/TuPigGHD2Chinese&Date)

## 🏘 感谢大家的群策群力

![Contributors](https://contrib.rocks/image?repo=Tupig/TuPigGHD2Chinese)

**上游项目**：[cngege/GitHubDesktop2Chinese](https://github.com/cngege/GitHubDesktop2Chinese)

<details>
<summary>点击展开示例图片</summary>
<img src="https://github.com/lkyero/GitHubDesktop_zh/assets/28597788/3023d028-8f63-4919-8900-ab3e953a1f76" alt="汉化效果展示图" />
</details>

---

# TuPigGHD2Chinese (English)

[简体中文](#tupigghd2chinese) | **English**

> This repository is a fork of [cngege/GitHubDesktop2Chinese](https://github.com/cngege/GitHubDesktop2Chinese), maintained and updated on top of the original work. All credit for the original idea and implementation goes to [CNGEGE](https://github.com/cngege).

> [!TIP]
> 📚 **Full documentation** is available in the [docs/wiki documentation center](docs/wiki/Home.md) (in Chinese): [Install & Usage](docs/wiki/安装与使用.md) · [Command-line Arguments](docs/wiki/命令行参数.md) · [FAQ](docs/wiki/常见问题.md) · [How It Works](docs/wiki/汉化原理.md) · [Mapping File Format](docs/wiki/映射文件格式.md) · [Regex Guide](docs/wiki/正则表达式指南.md) · [Build Guide](docs/wiki/编译指南.md) · [CI/CD](docs/wiki/CI-CD.md) · [Contributing](docs/wiki/贡献指南.md) · [Auto-maintain Tools](docs/wiki/自动维护工具.md)

## Contents

- [🥮 What is this](#-what-is-this)
- [🎯 How to use it](#-how-to-use-it)
- [⚙️ Command-line arguments](#-command-line-arguments)
- [🏗 Building from source](#-building-from-source)
- [👕 How to contribute translations](#-how-to-contribute-translations)
- [🍬 The mapping file localization.json](#-the-mapping-file-localizationjson)
- [🧪 CI / Auto-maintenance](#-ci--auto-maintenance)
- [📁 Project structure](#-project-structure)
- [🔭 Enabling GitHub Desktop preview options](#-enabling-github-desktop-preview-options)
- [🧭 FAQ](#-faq)
- [🎋 Features](#-features)
- [📦 Third-party libraries](#-third-party-libraries)

## 🥮 What is this

A program that automatically replaces the UI text of [GitHub Desktop](https://desktop.github.com/) ([official repository desktop/desktop](https://github.com/desktop/desktop)) with a target language (Chinese):

- **High compatibility**: replacement is driven by regex-based mappings, which tolerate the frequent version updates of GitHub Desktop
- **Low maintenance cost**: after a GitHub Desktop update, usually only a few broken translation entries need fixing
- **Self-updating**: the loader detects new releases automatically and supports one-click updates with resumable downloads

## 🎯 How to use it

[🎀 Video tutorial (BiliBili, in Chinese)](https://www.bilibili.com/video/BV17HpSeHEaC/) · [📖 Full tutorial: Install & Usage](docs/wiki/安装与使用.md)

**Option 1 (recommended)**: download `TuPigGHD2Chinese.exe` from [Releases](https://github.com/Tupig/TuPigGHD2Chinese/releases), double-click to run it, and the program fetches the latest `localization.json` over the network and localizes GitHub Desktop.

**Option 2**: download both `TuPigGHD2Chinese.exe` and `localization.json`, put them in the same folder, and run the program.

> [!IMPORTANT]
> - Prebuilt binaries are provided for **64-bit Windows / macOS (separate x64 and arm64 packages) / Linux (x64)**; pick the matching artifact from the [Releases](https://github.com/Tupig/TuPigGHD2Chinese/releases) page.
> - Release artifacts carry build provenance attestations (SLSA provenance); verify their origin with `gh attestation verify <downloaded file> --repo Tupig/TuPigGHD2Chinese`. The code-signing flow for the Windows binary is built in (SignPath integration) and activates automatically once credentials are configured; artifacts are currently unsigned.
> - Translation mappings are based on the **Windows** build of GitHub Desktop; texts on other platforms are mostly identical, but a few entries may not match.
> - After each GitHub Desktop update, run this program once again to re-apply the localization.

## ⚙️ Command-line arguments

> All arguments are optional; double-clicking without arguments runs the default flow (auto-locate the GitHub Desktop directory + fetch the latest mapping over the network).
>
> **Argument order**: `--nopause` is a program-level argument and must come **before** any subcommand (such as `dev`), e.g. `TuPigGHD2Chinese.exe --nopause dev --invalidcheck`.

### General arguments

| Argument | Description |
| --- | --- |
| `-h, --help` | Show help and exit |
| `-g, --githubdesktoppath <dir>` | Directory containing the GitHub Desktop js files (must contain `index.html`, `main.js`, `renderer.js`) |
| `-j, --json <path>` | Path to a local `localization.json` (must end with `.json`; if the file does not exist, a template is created and the program exits with a notice) |
| `-r, --onlyfromremote` | Read the mapping file from the remote repository only (ignore local) |
| `--rollback` | Restore the pre-localization state from `main.js.bak` / `renderer.js.bak` |
| `--nopause` | Do not pause before exiting (for scripts/automation; when the version requirement is not met, skips the prompt and forces the replacement) |

### Developer arguments (`dev` subcommand)

| Argument | Description |
| --- | --- |
| `-d, --dev` | Enter the developer options menu at startup (same as holding `Shift` at launch) |
| `--mainerrorcheck` | [Error-check mode] Check `main.js` entry by entry |
| `--rendererrorcheck` | [Error-check mode] Check `renderer.js` entry by entry |
| `--invalidcheck` | Detect invalid entries in `localization.json` (no match / illegal regex) |
| `--noreplaceres` | Do not replace resources (does not affect error-check mode) |
| `--translationfrombak` | Prefer reading js content from `.bak` backups for replacement (recommended together with `--invalidcheck`) |
| `--devreplace` | Replace only the mappings in `main_dev` / `renderer_dev` for quick debugging |
| `--devsetver` | Set the current program version (only available in the developer build `0.0.0-Dev`) |

### Common examples

```powershell
# Regular localization (auto-locate directory + fetch latest mapping)
TuPigGHD2Chinese.exe

# Specify the GitHub Desktop directory and a local mapping file
TuPigGHD2Chinese.exe -g "$env:LOCALAPPDATA\GitHubDesktop\app-<version>\resources\app" -j .\localization.json

# Use the remote mapping only (ignore the local file with the same name)
TuPigGHD2Chinese.exe -r

# Batch-check invalid entries (non-interactive: --nopause must come before dev)
TuPigGHD2Chinese.exe --nopause dev --invalidcheck

# GitHub Desktop fails to open after localization: restore from backup
TuPigGHD2Chinese.exe dev --translationfrombak

# Undo the localization and restore the original state
TuPigGHD2Chinese.exe --rollback
```

> [!TIP]
> Built-in help: `TuPigGHD2Chinese.exe --help`. For the full reference (developer menu, exit codes, error-check mode details) see [📖 Wiki: Command-line Arguments](docs/wiki/命令行参数.md).

## 🏗 Building from source

> The project uses CMake and supports three toolchains: **MSVC (Windows) / AppleClang (macOS) / GCC (Linux)**. Only 64-bit (x64 / arm64) builds are supported; other architectures fail at the CMake stage. **All released artifacts are 64-bit.** For per-platform setup and common build errors see [📖 Wiki: Build Guide](docs/wiki/编译指南.md).

1. Clone the repository
2. Open the project folder directly with **VS2022** (opened via CMake)
3. Build with the `x64-debug` / `x64-release` presets (presets target Windows MSVC/Ninja; on macOS/Linux use the command line below)

Command-line build (works on Windows / macOS / Linux):

```powershell
cmake -B build
cmake --build build --config Release
```

> [!NOTE]
> OpenSSL dependency: the static libraries and headers are committed to the repository on all three platforms (Windows `third_party/openssl/`, macOS `third_party/openssl-macos-{x64,arm64}/`, Linux `third_party/openssl-linux-x64/`; no preinstall or download needed — see [`third_party/README.md`](third_party/README.md) for versions and update instructions). **CI-released macOS artifacts are split per architecture (`macos-x64` / `macos-arm64`); OpenSSL is statically linked on every platform, with the macOS artifacts additionally trimmed via strip/LTO** — self-contained with no extra dependencies.

## 👕 How to contribute translations

1. Clone the repository and read [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md) (see [📖 Wiki: Contributing Guide](docs/wiki/贡献指南.md) for the detailed workflow, and [📖 Wiki: Regex Guide](docs/wiki/正则表达式指南.md) for regex conventions)
2. Add translation entries to [`json/localization.json`](json/localization.json) following the existing format
3. Open a PR

**Debugging tip**: put new entries into `main_dev` or `renderer_dev` first, hold `Shift` at startup, and enable "replace only the specified mappings" for quick testing. Once verified, move them into `main` / `renderer` following the entry-order rules in [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md) (usually append at the end; if a shorter entry is contained in a broader one, it must be placed before that entry), then open the PR.

## 🍬 The mapping file localization.json

Stores the mapping from English GitHub Desktop texts to Chinese, applied via regex matching. Day-to-day maintenance of this project is mostly about editing this file (see [📖 Wiki: Mapping File Format](docs/wiki/映射文件格式.md) for fields and entry rules, and [📖 Wiki: Regex Guide](docs/wiki/正则表达式指南.md) for regex conventions).

- Path: `json/localization.json`

| Node | Type | Description |
| --- | --- | --- |
| `version` | int | JSON file format version; bumped only when the format changes |
| `minversion` | string | Minimum loader version required |
| `tip` | string[] | Notices displayed in the loader |
| `select` | object[] | Entries offered as optional changes during localization |
| `main` | array | Mappings applied to `main.js` |
| `main_dev` | array | Quick-replacement mappings for `main.js` development |
| `renderer` | array | Mappings applied to `renderer.js` |
| `renderer_dev` | array | Quick-replacement mappings for `renderer.js` development |

Structure of a `select` element:

| Field | Type | Description |
| --- | --- | --- |
| `replaceFile` | string | Target file (`main.js` / `renderer.js`) |
| `tooltip` | string | Notice text |
| `enable` | bool | Whether this entry is enabled |
| `replace` | string[][] | 2-D replacement array: `[find regex, replacement text, optional find regex]` |

## 🧪 CI / Auto-maintenance

This repository uses a single unified workflow, [`tupigghd2chinese.yml`](.github/workflows/tupigghd2chinese.yml) (see [📖 Wiki: CI/CD](docs/wiki/CI-CD.md) for the job matrix, release rules, and quality gates):

| Function | Triggers | Description |
| --- | --- | --- |
| Build | push to `main` / PR (targeting main) / tag `v*` / scheduled / manual auto, build, release | Build matrix + artifact functional tests: Windows x64, macOS x64 / arm64 (per-architecture packages), Linux x64 |
| JSON quality checks | PR (targeting main) / push `main` / tag `v*` / scheduled / manual auto, security, release | Regex validity, structural integrity, placeholder checks, blacklist of syntax unsupported by std::regex, full-width question-mark style in translations |
| Tool self-checks | PR (targeting main) / push `main` / tag `v*` / scheduled / manual auto, security, maintain, release | Auto-maintain tool syntax check + unit tests + Markdown link check |
| CodeQL scan | PR (targeting main) / push `main` / manual auto, security, release | C/C++ security scanning |
| Invalid-entry detection + candidate extraction | push `main` / scheduled (daily) / manual auto, maintain, release | Detect broken mappings, extract untranslated candidates, auto-create/close issues |
| Release | push `main` (when changed) / tag `v*` / scheduled (when changed) / manual auto (main only, when changed), release (main only) | Auto-bump the version number and publish multi-platform artifacts + localization.json |

> A manual `type` runs only the corresponding subset; `version` is an optional override — **leave it empty to auto-increment the version** (e.g. `0.0.1 → 0.0.2`; a full patch of 100 carries into minor); if filled in it must be plain `x.y.z` digits, otherwise CI fails at the version validation step.

> [!TIP]
> Manual `type=auto` runs the full pipeline (build → checks → maintenance → release) and auto-increments the version (e.g. `0.0.1 → 0.0.2`); you can also specify `version` in the input box (`x.y.z` format).

The auto-maintain tool lives in [`tools/auto-maintain`](tools/auto-maintain) and detects against the **Windows** build of GitHub Desktop's `main.js` / `renderer.js`; run it locally with:

```powershell
cd tools/auto-maintain
npm run all        # invalid-entry detection + untranslated candidate extraction; report written to ghdesktop-auto-maintain/report.md in the system temp directory
```

> [!NOTE]
> **Scheduling and release rules**
> - **Scheduled runs**: build and auto-maintenance run daily at 10:00 Beijing time (02:00 UTC).
> - **Change detection**: actual changes to `json/`, `src/`, `third_party/`, `CMakeLists.txt`, `CMakePresets.json` are detected against the latest `v*` tag.
> - **Conditional releases**: push to `main`, scheduled runs, and manual `auto` publish a new version (patch number +1) only when such changes are detected; otherwise the release step is skipped to avoid empty versions. `type=release` and pushed tags always publish.

## 📁 Project structure

Architecture overview: a C++ main program (`src/`, loader + localizer, producing a single-file exe), one localization mapping file (`json/localization.json`, loaded from disk or the network at runtime), a Node.js auto-maintain tool (`tools/auto-maintain`), and a unified CI workflow (`.github/workflows`, with CI scripts in `tools/ci`).

```text
.
├── src/                          # C++ main program sources
│   ├── TuPigGHD2Chinese.cpp # Entry point and main localization flow
│   ├── TuPigGHD2Chinese.h
│   ├── utils/                    # Shared utilities (umbrella header + per-responsibility subheaders)
│   │   ├── utils.hpp             # Umbrella header (the only public entry)
│   │   ├── encoding.hpp          # Encoding conversion (wide/narrow strings, path building)
│   │   ├── system.hpp            # Environment variables and system proxy detection
│   │   ├── fs_io.hpp             # File I/O
│   │   ├── http.hpp              # HTTP / TLS validation / auto-update
│   │   └── input.hpp             # Interactive input
│   └── version/Version.hpp       # Version parsing (self-written code)
├── third_party/                  # Third-party dependencies (committed to the repo, incl. prebuilt OpenSSL static libs for all platforms)
│   ├── include/                  # CLI11, cpp-httplib, nlohmann/json, spdlog, WinReg
│   ├── openssl/                  # OpenSSL 3.5.9 headers and Windows prebuilt static libraries
│   ├── openssl-macos-{x64,arm64}/ # OpenSSL 3.5.9 macOS prebuilt static libraries (per architecture)
│   ├── openssl-linux-x64/        # OpenSSL 3.5.9 Linux prebuilt static libraries (-fPIC)
│   └── README.md                 # Third-party version list and local build/update instructions
├── json/
│   └── localization.json         # Localization mapping (core data; the path is a public contract of released binaries and must not move)
├── tools/
│   ├── auto-maintain/            # localization.json auto-maintain tool (invalid-entry detection / candidate extraction)
│   └── ci/                       # CI scripts (data quality checks / tool self-checks / Markdown link check / smoke tests / maintenance report)
├── docs/                         # Documentation
│   ├── README.md                 # Documentation index and categories
│   ├── CONTRIBUTING.md           # Translation contribution guide (auto-detected by GitHub)
│   ├── ReleaseBody.md            # Release note template (assembled into release notes by CI)
│   └── wiki/                     # In-depth wiki docs (usage / internals / contributing / development)
├── .github/
│   └── workflows/                # Unified CI/CD workflow (the repository's only workflow)
├── SECURITY.md                   # Security policy (private vulnerability reporting / supply chain notes)
├── CMakeLists.txt
└── CMakePresets.json
```

## 🔭 Enabling GitHub Desktop preview options

How GitHub Desktop internally decides whether preview features are on:

```javascript
const nn = !1;
function rn() {
    return !nn && "1" === process.env.GITHUB_DESKTOP_PREVIEW_FEATURES
}
// when rn() returns true, preview mode is enabled
```

**Option 1**: set the environment variable, then start GitHub Desktop

```cmd
set GITHUB_DESKTOP_PREVIEW_FEATURES=1
"GitHub Desktop.lnk"
```

**Option 2**: choose as prompted while running the loader, which enables the preview features automatically (these are the optional replacements under `select`).

## 🧭 FAQ

> [!TIP]
> **Cannot find the OpenSSL DLLs**: please update to the [latest release](https://github.com/Tupig/TuPigGHD2Chinese/releases).
>
> **"The mapping file requires loader version …" / version requirement not met**: the remote mapping targets only the latest loader; please upgrade the loader from [Releases](https://github.com/Tupig/TuPigGHD2Chinese/releases). You can also type `f` at the prompt to force the replacement (not recommended — some entries may not match).
>
> **The program does not run / flashes and exits / `MSVCP140_ATOMIC_WAIT.dll` is missing**: install the **x64** build of the [Microsoft Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170) (`vc_redist.x64.exe`).
>
> If it still fails after installing the latest VC++ runtime, check the program folder for leftover files such as `MSVCP140.dll` or `VCRUNTIME140.dll` and delete them.
>
> **GitHub Desktop fails to open after localization**: after updating the loader, run `TuPigGHD2Chinese.exe dev --translationfrombak`.
>
> **Want to undo the localization**: run `TuPigGHD2Chinese.exe --rollback` (restores from `main.js.bak` / `renderer.js.bak`).
>
> **Is macOS / Linux supported?**: per-architecture macOS binaries are provided (Intel: download `macos-x64`; Apple Silicon: `macos-arm64`) along with a Linux x64 build (from the Release page). The mappings target the **Windows** build of GitHub Desktop's UI texts (including Windows-specific content such as `&` access keys); a few entries may not match on other platforms.
>
> **Scripted / automated runs**: add `--nopause` for non-interactive use; note that it is a program-level argument and must come before subcommands such as `dev`.
>
> More topics (proxy not taking effect, `#{n}` placeholders, resumable downloads, `--invalidcheck`, etc.) in [📖 Wiki: FAQ](docs/wiki/常见问题.md).

Suggestions are welcome in [Issues](https://github.com/Tupig/TuPigGHD2Chinese/issues).

Please report security vulnerabilities privately as described in [`SECURITY.md`](SECURITY.md); build provenance of released artifacts can be verified with `gh attestation verify <file> --repo Tupig/TuPigGHD2Chinese`.

## 🎋 Features

- [x] JSON file declaring its format version and the minimum loader version
- [x] Loader program version defined via a macro
- [x] Third element of a replacement entry (find parameter) used as a global regex lookup, with `#{number}` placeholder backfill
- [x] Minimum-version check that prompts or asks before forcing a replacement
- [x] Pause confirmation (wait for a key before exiting; when `minversion` is not met, force only after typing `f`) — skipped entirely by `--nopause` (note: when the version requirement is not met, the prompt is skipped and the replacement is forced)
- [x] Automatic update detection with one-click update + resumable download
- [x] Extra description text (`tip`) from the JSON displayed in the loader
- [x] Project contributors displayed after localization completes
- [x] Recovery from backups when localization fails
- [x] Selective localization (`select` prompts)
- [x] One-click enablement of preview features
- [x] System HTTP proxy support (environment variables + registry)
- [x] Reads the latest GitHub Desktop version and compares it with the local one

## 📦 Third-party libraries

Thanks to these excellent open-source projects:

| Library | Purpose | Repository |
| --- | --- | --- |
| CLI11 | Command-line parsing | [CLIUtils/CLI11](https://github.com/CLIUtils/CLI11) |
| cpp-httplib | HTTP client | [yhirose/cpp-httplib](https://github.com/yhirose/cpp-httplib) |
| nlohmann/json | JSON parsing | [nlohmann/json](https://github.com/nlohmann/json) |
| spdlog | Logging | [gabime/spdlog](https://github.com/gabime/spdlog) |
| WinReg | Windows registry access | [GiovanniDicanio/WinReg](https://github.com/GiovanniDicanio/WinReg) |
| OpenSSL | SSL/TLS support | [openssl/openssl](https://github.com/openssl/openssl) |

### ⚖️ Third-party license notices

Each Release ships [`THIRD_PARTY_NOTICES.txt`](THIRD_PARTY_NOTICES.txt)
(copyright lines and license terms for all bundled libraries, as required by
BSD-3-Clause / Apache-2.0 / MIT for binary redistribution). The same file is
also available in the repository root.

## 🏘 Credits

![Contributors](https://contrib.rocks/image?repo=Tupig/TuPigGHD2Chinese)

**Upstream project**: [cngege/GitHubDesktop2Chinese](https://github.com/cngege/GitHubDesktop2Chinese)

<details>
<summary>Click to expand the screenshot</summary>
<img src="https://github.com/lkyero/GitHubDesktop_zh/assets/28597788/3023d028-8f63-4919-8900-ab3e953a1f76" alt="Localization preview" />
</details>
