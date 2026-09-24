# GitHubDesktop2Chinese

[![CI](https://img.shields.io/github/actions/workflow/status/Tupig/GitHubDesktop2Chinese/ghdesktop2chinese.yml?branch=main&label=CI)](https://github.com/Tupig/GitHubDesktop2Chinese/actions/workflows/ghdesktop2chinese.yml)
[![Release](https://img.shields.io/github/v/release/Tupig/GitHubDesktop2Chinese)](https://github.com/Tupig/GitHubDesktop2Chinese/releases)
[![License](https://img.shields.io/github/license/Tupig/GitHubDesktop2Chinese)](LICENSE.txt)
[![C++](https://img.shields.io/badge/C%2B%2B-20-blue?logo=cplusplus&logoColor=white)](#-怎么编译源代码)
[![localization.json](https://img.shields.io/badge/localization.json-v3-green)](#-映射文件localizationjson)

> 本仓库派生（fork）自 [cngege/GitHubDesktop2Chinese](https://github.com/cngege/GitHubDesktop2Chinese)，在此基础上继续维护与更新。原作者 [CNGEGE](https://github.com/cngege)，感谢其开创性工作。

## 目录

- [🥮 这是什么](#-这是什么)
- [🎯 怎么使用它](#-怎么使用它)
- [🏗️ 怎么编译源代码](#️-怎么编译源代码)
- [👕 怎么贡献汉化](#-怎么贡献汉化)
- [🍬 映射文件 localization.json](#-映射文件localizationjson)
- [🧪 CI / 自动维护](#-ci--自动维护)
- [📁 项目结构](#-项目结构)
- [🔭 开启 GitHub Desktop 预览版选项](#-开启-github-desktop-预览版选项)
- [🧭 常见问题](#-常见问题)
- [🎋 功能特性](#-功能特性)
- [第三方库](#第三方库)
- [⭐ 星标历史](#-星标历史)

## 🥮这是什么

一个自动替换 GitHub Desktop 界面文本为目标语言（中文）的程序：

- **高兼容性**：采用正则映射替换，对 GitHub Desktop 频繁更新的版本变化兼容性高
- **低维护成本**：版本更新后仅需手动补充个别失效翻译条目
- **可自动更新**：加载器自动检测新版本，支持一键更新与断点续传

## 🎯怎么使用它

[🎀 视频教程（BiliBili）](https://www.bilibili.com/video/BV17HpSeHEaC/)

**方式一（推荐）**：前往 [Releases](https://github.com/Tupig/GitHubDesktop2Chinese/releases) 下载 `GitHubDesktop2Chinese.exe`，双击运行，程序自动联网获取最新 `localization.json` 完成汉化。

**方式二**：下载 `GitHubDesktop2Chinese.exe` 与 `localization.json`，放在同一文件夹后运行。

> [!IMPORTANT]
> - 本程序**仅支持 64 位 Windows**（x64），不提供 32 位版本。
> - GitHub Desktop 每次版本更新后，都需要重新运行一次本程序才能完成汉化。

## 🏗️怎么编译源代码

> 项目**仅支持 64 位（x64）构建**，使用其他架构配置会在 CMake 阶段直接报错。

1. 克隆仓库
2. 使用 **VS2022** 直接打开项目文件夹（通过 CMake 打开）
3. 选择 `x64-debug` / `x64-release` 预设进行构建

命令行构建（可选）：

```powershell
cmake -B build -A x64
cmake --build build --config Release
```

## 👕怎么贡献汉化

1. 克隆仓库，阅读 [`docs/关于一些注意事项.txt`](docs/关于一些注意事项.txt)
2. 在 [`json/localization.json`](json/localization.json) 中参照已有格式补充翻译条目
3. 提交 PR

**开发调试技巧**：新写的条目可先放入 `main_dev` 或 `renderer_dev`，然后按住 `Shift` 启动程序，开启「仅替换指定映射项」进行快速测试；完成后将条目移动到 `main` 或 `renderer` 数组末尾再提交 PR。

## 🍬映射文件 localization.json

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

## 🧪CI / 自动维护

本仓库使用统一工作流 [`ghdesktop2chinese.yml`](.github/workflows/ghdesktop2chinese.yml)：

| 功能 | 触发方式 | 说明 |
| --- | --- | --- |
| 构建 | PR / tag `v*` / 手动 | 仅 64 位（x64）构建 |
| JSON 质量校验 | PR / 手动 | 正则合法性、结构完整性、占位符检查 |
| 工具自检 | PR / 手动 | 自动维护工具语法检查 + 单元测试 |
| CodeQL 扫描 | PR / 手动 | C/C++ 安全扫描 |
| 失效检测 + 候选提取 | 手动 | 检测失效映射、提取未翻译候选，自动创建/关闭 Issue |
| Release 发布 | tag `v*` / 手动 auto、release | 自动升级版本号，发布 exe + localization.json |

> [!TIP]
> 手动触发 `type=auto` 会跑完整链路（构建 → 检查 → 维护 → 发布），版本号自动升级补丁号（如 `1.2.4 → 1.2.5`）；也可在 `version` 输入框手动指定。
> 若自上个版本以来 `json/`、`src/`、`third_party/`、CMake 均无变更，`auto` 模式会**自动跳过发布**，避免产生空版本；`type=release` 与 tag 推送则始终发布。

自动维护工具位于 [`tools/auto-maintain`](tools/auto-maintain)，基于 **Windows 版** GitHub Desktop 的 `main.js` / `renderer.js` 检测；本地运行：

```powershell
cd tools/auto-maintain
npm run all        # 失效检测 + 未翻译候选提取
```

> [!NOTE]
> 为避免自动消耗 Actions 额度，定时任务已移除，全部改为 PR 或手动触发。

## 📁 项目结构

```text
.
├── src/                          # 项目源码
│   ├── GitHubDesktop2Chinese.cpp # 程序入口与汉化主流程
│   ├── GitHubDesktop2Chinese.h
│   └── Utils/utils.hpp           # HTTP / 代理 / 文件等通用工具
├── third_party/                  # 第三方依赖（随仓库提交，构建无需联网下载）
│   ├── include/                  # CLI11、cpp-httplib、nlohmann/json、spdlog、WinReg、VersionParse
│   └── openssl/                  # OpenSSL 头文件与预编译库（x64）
├── json/
│   └── localization.json         # 汉化映射（核心数据）
├── tools/auto-maintain/          # localization.json 自动维护工具（失效检测 / 候选提取）
├── docs/                         # 文档（贡献注意事项、Release 说明）
├── .github/workflows/            # CI/CD 工作流
├── CMakeLists.txt
└── CMakePresets.json
```

## 🔭开启 GitHub Desktop 预览版选项

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

## 🧭常见问题

> [!TIP]
> **找不到 openssl 的 DLL**：请更新到 [最新版本](https://github.com/Tupig/GitHubDesktop2Chinese/releases)。
>
> **程序不运行 / 一闪而过 / 缺失 `MSVCP140_ATOMIC_WAIT.dll`**：安装 [Microsoft Visual C++ Redistributable](https://learn.microsoft.com/zh-cn/cpp/windows/latest-supported-vc-redist?view=msvc-170) 的 **x64** 版本（`vc_redist.x64.exe`）。
>
> 安装最新 VC++ 运行库后仍无法运行时，请检查程序目录下是否残留 `MSVCP140.dll`、`VCRUNTIME140.dll` 等文件，如有请删除。
>
> **汉化后主程序无法打开**：更新加载器后执行 `GitHubDesktop2Chinese.exe dev --translationfrombak`。

有任何建议欢迎提 [Issues](https://github.com/Tupig/GitHubDesktop2Chinese/issues)。

## 🎋功能特性

- [x] JSON 格式标识文件版本与最低加载器版本
- [x] 加载器程序版本宏定义
- [x] 替换映射第三项（查找参数）全局正则查找，`#{number}` 占位符回填
- [x] 最低版本校验，不满足时提示或询问是否强制替换
- [x] 替换前暂停确认（`--nopause` 可跳过）
- [x] 自动检测更新，一键更新 + 断点续传
- [x] JSON 附加描述文本（`tip`）在加载器中显示
- [x] 汉化完成后显示项目参与者
- [x] 汉化异常后从备份恢复
- [x] 选择性汉化（`select` 提示项）
- [x] 预览版功能一键开启
- [x] 系统 HTTP 代理支持（环境变量 + 注册表）
- [x] 读取 GitHub Desktop 最新版与本地版本对比提示

## 第三方库

感谢以下优质开源项目：

| 库 | 用途 | 仓库 |
| --- | --- | --- |
| CLI11 | 命令行参数解析 | [CLIUtils/CLI11](https://github.com/CLIUtils/CLI11) |
| cpp-httplib | HTTP 客户端 | [yhirose/cpp-httplib](https://github.com/yhirose/cpp-httplib) |
| nlohmann/json | JSON 解析 | [nlohmann/json](https://github.com/nlohmann/json) |
| spdlog | 日志输出 | [gabime/spdlog](https://github.com/gabime/spdlog) |
| WinReg | Windows 注册表操作 | [GiovanniDicanio/WinReg](https://github.com/GiovanniDicanio/WinReg) |
| OpenSSL | SSL/TLS 支持 | [openssl/openssl](https://github.com/openssl/openssl) |

## ⭐星标历史

[![Star History Chart](https://api.star-history.com/svg?repos=Tupig/GitHubDesktop2Chinese&type=Date)](https://star-history.com/#Tupig/GitHubDesktop2Chinese&Date)

## 🏘️感谢大家的群策群力

![Contributors](https://contrib.rocks/image?repo=Tupig/GitHubDesktop2Chinese)

**上游项目**：[cngege/GitHubDesktop2Chinese](https://github.com/cngege/GitHubDesktop2Chinese)

<details>
<summary>点击展开示例图片</summary>
<img src="https://github.com/lkyero/GitHubDesktop_zh/assets/28597788/3023d028-8f63-4919-8900-ab3e953a1f76" alt="汉化效果展示图" />
</details>
