# GitHubDesktop2Chinese

[![CI](https://img.shields.io/github/actions/workflow/status/Tupig/GitHubDesktop2Chinese/ghdesktop2chinese.yml?branch=main&label=CI)](https://github.com/Tupig/GitHubDesktop2Chinese/actions/workflows/ghdesktop2chinese.yml)
[![Release](https://img.shields.io/github/v/release/Tupig/GitHubDesktop2Chinese)](https://github.com/Tupig/GitHubDesktop2Chinese/releases)
[![License](https://img.shields.io/github/license/Tupig/GitHubDesktop2Chinese)](LICENSE.txt)
[![C++](https://img.shields.io/badge/C%2B%2B-20-blue?logo=cplusplus&logoColor=white)](#-怎么编译源代码)
[![localization.json](https://img.shields.io/badge/localization.json-v3-green)](#-映射文件-localizationjson)

> 本仓库派生（fork）自 [cngege/GitHubDesktop2Chinese](https://github.com/cngege/GitHubDesktop2Chinese)，在此基础上继续维护与更新。原作者 [CNGEGE](https://github.com/cngege)，感谢其开创性工作。

## 目录

- [🥮 这是什么](#-这是什么)
- [🎯 怎么使用它](#-怎么使用它)
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

[🎀 视频教程（BiliBili）](https://www.bilibili.com/video/BV17HpSeHEaC/)

**方式一（推荐）**：前往 [Releases](https://github.com/Tupig/GitHubDesktop2Chinese/releases) 下载 `GitHubDesktop2Chinese.exe`，双击运行，程序自动联网获取最新 `localization.json` 完成汉化。

**方式二**：下载 `GitHubDesktop2Chinese.exe` 与 `localization.json`，放在同一文件夹后运行。

> [!IMPORTANT]
> - 本程序提供 **64 位 Windows / macOS（Intel 与 Apple Silicon）/ Linux（x64）** 多平台版本，从 [Releases](https://github.com/Tupig/GitHubDesktop2Chinese/releases) 页选择对应产物下载。
> - 汉化映射以 **Windows 版** GitHub Desktop 文案为准，其他平台文案基本一致，个别条目可能不匹配。
> - GitHub Desktop 每次版本更新后，都需要重新运行一次本程序才能完成汉化。

## 🏗 怎么编译源代码

> 项目基于 CMake，支持 **MSVC（Windows）/ AppleClang（macOS）/ GCC（Linux）** 三套工具链，仅支持 64 位（x64 / arm64）构建，其他架构会在 CMake 阶段直接报错。

1. 克隆仓库
2. 使用 **VS2022** 直接打开项目文件夹（通过 CMake 打开）
3. 选择 `x64-debug` / `x64-release` 预设进行构建

命令行构建（Windows / macOS / Linux 通用）：

```powershell
cmake -B build
cmake --build build --config Release
```

> [!NOTE]
> OpenSSL 依赖：Windows 使用 CMake 内置下载（无需预装）；macOS 执行 `brew install openssl`，Linux 安装 `libssl-dev` 后 CMake 会通过 `find_package(OpenSSL)` 自动发现。

## 👕 怎么贡献汉化

1. 克隆仓库，阅读 [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md)
2. 在 [`json/localization.json`](json/localization.json) 中参照已有格式补充翻译条目
3. 提交 PR

**开发调试技巧**：新写的条目可先放入 `main_dev` 或 `renderer_dev`，然后按住 `Shift` 启动程序，开启「仅替换指定映射项」进行快速测试；完成后将条目移动到 `main` 或 `renderer` 数组末尾再提交 PR。

## 🍬 映射文件 localization.json

存储 GitHub Desktop 英文文本到中文文本的映射，通过正则匹配完成替换。项目日常更新主要维护此文件。

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

本仓库使用统一工作流 [`ghdesktop2chinese.yml`](.github/workflows/ghdesktop2chinese.yml)：

| 功能 | 触发方式 | 说明 |
| --- | --- | --- |
| 构建 | push `main` / PR / tag `v*` / 定时 / 手动 auto、build、release | 四目标构建 + 产物功能测试：Windows x64、macOS Intel、macOS Apple Silicon、Linux x64 |
| JSON 质量校验 | PR / tag `v*` / 定时 / 手动 auto、security、release | 正则合法性、结构完整性、占位符检查、std::regex 不兼容语法黑名单、译文问句全角风格 |
| 工具自检 | PR / tag `v*` / 定时 / 手动 auto、security、maintain、release | 自动维护工具语法检查 + 单元测试 |
| CodeQL 扫描 | PR / push `main` / 手动 auto、security | C/C++ 安全扫描 |
| 失效检测 + 候选提取 | 定时（每日）/ 手动 auto、maintain | 检测失效映射、提取未翻译候选，自动创建/关闭 Issue |
| Release 发布 | tag `v*` / 定时（有变更时）/ 手动 auto（有变更）、release | 自动升级版本号，发布四平台产物 + localization.json |

> 手动触发的 `type` 除表中所列外（`build` 等）仅执行对应子集，`version` 输入须为 `x.y.z` 纯数字格式，否则 CI 在版本号校验处直接失败。

> [!TIP]
> 手动触发 `type=auto` 会跑完整链路（构建 → 检查 → 维护 → 发布），版本号自动升级补丁号（如 `1.2.4 → 1.2.5`）；也可在 `version` 输入框手动指定（`x.y.z` 格式）。

自动维护工具位于 [`tools/auto-maintain`](tools/auto-maintain)，基于 **Windows 版** GitHub Desktop 的 `main.js` / `renderer.js` 检测；本地运行：

```powershell
cd tools/auto-maintain
npm run all        # 失效检测 + 未翻译候选提取, 报告写入系统临时目录 ghdesktop-auto-maintain/report.md
```

> [!NOTE]
> **定时与发布规则**
> - **定时调度**：每日北京时间 10:00（UTC 02:00）自动执行构建与自动维护。
> - **变更检测**：以最新 `v*` tag 为基准，检测 `json/`、`src/`、`third_party/`、`CMakeLists.txt`、`CMakePresets.json` 的实际变更。
> - **条件发布**：定时与手动 `auto` 仅在检测到上述变更时发布新版本（补丁号 +1）；无变更自动跳过，避免空版本。`type=release` 与推送 tag 始终发布。

## 📁 项目结构

架构概览：一个 C++ 主程序（`src/`，加载器 + 汉化器，产物为单文件 exe）、一份汉化映射数据（`json/localization.json`，运行时从本地或远程加载）、一个 Node.js 自动维护工具（`tools/auto-maintain`）以及统一 CI 工作流（`.github/workflows`）。

```text
.
├── src/                          # C++ 主程序源码
│   ├── GitHubDesktop2Chinese.cpp # 程序入口与汉化主流程
│   ├── GitHubDesktop2Chinese.h
│   ├── utils/                    # 通用工具（伞形入口 + 按职责拆分的子头）
│   │   ├── utils.hpp             # 伞形汇总头（对外唯一入口）
│   │   ├── encoding.hpp          # 编码转换（宽窄字符串 / 路径构造）
│   │   ├── system.hpp            # 环境变量与系统代理探测
│   │   ├── fs_io.hpp             # 文件读写
│   │   ├── http.hpp              # HTTP / TLS 校验 / 自动更新
│   │   └── input.hpp             # 交互输入
│   └── version/Version.hpp       # 版本号解析（自研代码）
├── third_party/                  # 第三方依赖（随仓库提交，含预编译 OpenSSL x64 静态库）
│   ├── include/                  # CLI11、cpp-httplib、nlohmann/json、spdlog、WinReg
│   └── openssl/                  # OpenSSL 头文件与预编译库
├── json/
│   └── localization.json         # 汉化映射（核心数据；路径为已发布程序的公共契约，不可移动）
├── tools/auto-maintain/          # localization.json 自动维护工具（失效检测 / 候选提取）
├── docs/                         # 文档
│   ├── CONTRIBUTING.md           # 汉化贡献指南（GitHub 自动识别）
│   └── ReleaseBody.md            # Release 说明模板（CI 拼接进发布说明）
├── .github/workflows/            # CI/CD 工作流
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
> **找不到 openssl 的 DLL**：请更新到 [最新版本](https://github.com/Tupig/GitHubDesktop2Chinese/releases)。
>
> **程序不运行 / 一闪而过 / 缺失 `MSVCP140_ATOMIC_WAIT.dll`**：安装 [Microsoft Visual C++ Redistributable](https://learn.microsoft.com/zh-cn/cpp/windows/latest-supported-vc-redist?view=msvc-170) 的 **x64** 版本（`vc_redist.x64.exe`）。
>
> 安装最新 VC++ 运行库后仍无法运行时，请检查程序目录下是否残留 `MSVCP140.dll`、`VCRUNTIME140.dll` 等文件，如有请删除。
>
> **汉化后主程序无法打开**：更新加载器后执行 `GitHubDesktop2Chinese.exe dev --translationfrombak`。
>
> **想撤销汉化、还原为汉化前状态**：执行 `GitHubDesktop2Chinese.exe --rollback`（从 `main.js.bak` / `renderer.js.bak` 还原）。
>
> **是否支持 macOS / Linux？**：提供 macOS（Intel / Apple Silicon）与 Linux x64 版本（Release 页下载）。汉化映射针对 **Windows 版** GitHub Desktop 的界面文案（含 `&` 访问键等 Windows 专属内容），其他平台个别条目可能不匹配。

有任何建议欢迎提 [Issues](https://github.com/Tupig/GitHubDesktop2Chinese/issues)。

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

[![Star History Chart](https://api.star-history.com/svg?repos=Tupig/GitHubDesktop2Chinese&type=Date)](https://star-history.com/#Tupig/GitHubDesktop2Chinese&Date)

## 🏘 感谢大家的群策群力

![Contributors](https://contrib.rocks/image?repo=Tupig/GitHubDesktop2Chinese)

**上游项目**：[cngege/GitHubDesktop2Chinese](https://github.com/cngege/GitHubDesktop2Chinese)

<details>
<summary>点击展开示例图片</summary>
<img src="https://github.com/lkyero/GitHubDesktop_zh/assets/28597788/3023d028-8f63-4919-8900-ab3e953a1f76" alt="汉化效果展示图" />
</details>
