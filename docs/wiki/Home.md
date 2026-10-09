# GitHubDesktop2Chinese 文档中心

> 本目录是项目的 **Wiki 文档中心**，内容比 README 更详细、成体系。
> 如需将本目录发布到 GitHub Wiki，把各 `.md` 文件推送至仓库的 `wiki` 管理仓库（`<用户名>/<仓库名>.wiki.git`）即可，页面名即文件名（`_Sidebar.md` / `_Footer.md` 会被 GitHub Wiki 自动识别为侧边栏与页脚）。

[GitHubDesktop2Chinese](https://github.com/Tupig/GitHubDesktop2Chinese) 是一个把 [GitHub Desktop](https://desktop.github.com/) 界面文本自动替换为中文的工具：**无需重打包、无需改安装器**，加载器直接对 GitHub Desktop 的 `main.js` / `renderer.js` 做「正则映射替换」，并全程保留 `.bak` 备份、可一键回滚。

- 上游原作：[cngege/GitHubDesktop2Chinese](https://github.com/cngege/GitHubDesktop2Chinese)（感谢 CNGEGE 的开创性工作）
- 视频教程：[BiliBili](https://www.bilibili.com/video/BV17HpSeHEaC/)

---

## 快速开始（30 秒版）

1. 到 [Releases](https://github.com/Tupig/GitHubDesktop2Chinese/releases) 下载对应平台的 `GitHubDesktop2Chinese` 可执行文件（Windows x64 / macOS 通用二进制 / Linux x64）。
2. 双击运行（macOS / Linux 在终端中运行），程序会：
   - 自动在注册表中定位 GitHub Desktop 安装目录（Windows）；
   - 自动联网拉取最新 `localization.json` 汉化映射；
   - 自动备份 `main.js` / `renderer.js` 为 `.bak`；
   - 按映射逐条替换并原子写回。
3. 重新打开 GitHub Desktop，界面即变为中文。

> GitHub Desktop 每次自动更新后，汉化会被新版本覆盖，**重新运行一次本程序**即可。

遇到问题？先看 [常见问题](常见问题.md)。

---

## 文档导航

### 使用者

| 页面 | 内容 |
| --- | --- |
| [安装与使用](安装与使用.md) | 下载渠道、两种运行方式、运行全流程逐步解读、代理与离线场景、预览版功能开启 |
| [命令行参数](命令行参数.md) | 全部命令行参数速查、参数顺序规则、开发者菜单（Shift）、退出码含义、可复制示例 |
| [常见问题](常见问题.md) | 一闪而过 / DLL 缺失 / 汉化后打不开 / 还原 / 跨平台路径等故障排查 |

### 贡献者

| 页面 | 内容 |
| --- | --- |
| [贡献指南](贡献指南.md) | 从克隆到提 PR 的完整流程、本地验证方法、发布说明写法 |
| [映射文件格式](映射文件格式.md) | `localization.json` 每个字段的语义、条目三元素写法、`#{n}` 占位符回填机制 |
| [正则表达式指南](正则表达式指南.md) | 转义规则、`std::regex` 兼容性黑名单、「短含长」排序、译文风格约定 |
| [自动维护工具](自动维护工具.md) | `tools/auto-maintain` 失效检测与候选提取的原理、参数、报告解读 |

### 开发者

| 页面 | 内容 |
| --- | --- |
| [汉化原理](汉化原理.md) | 架构设计、目录自动探测、备份与原子写入、三元素回填、自动更新机制 |
| [编译指南](编译指南.md) | 三平台（Windows / macOS / Linux）环境准备、CMake 预设、OpenSSL 处理、常见构建错误 |
| [CI-CD](CI-CD.md) | 统一工作流的七个 job、触发矩阵、版本号递增与发布规则、质量门 |

### 其他

- [安全政策](../SECURITY.md)：私密漏洞报告渠道、供应链与校验说明
- [Release 说明模板](ReleaseBody.md)：CI 如何把变更行拼进发布说明
- [项目结构](../README.md#-项目结构)：仓库目录树速览

---

## 设计目标

| 目标 | 手段 |
| --- | --- |
| 高兼容性 | 正则映射替换而非硬编码偏移，GitHub Desktop 频繁更新时仅个别条目失效 |
| 低维护成本 | 每日 CI 自动检测失效条目并提取未翻译候选，人工只需补翻译 |
| 可恢复 | 首次汉化前创建 `.bak` 备份；处理异常时自动回滚；`--rollback` 一键还原 |
| 可自动更新 | 加载器自检新版本，浏览器直链下载 + SHA256 校验 + 断点续传 |
| 跨平台 | C++20 + CMake，一份代码三个工具链（MSVC / AppleClang / GCC） |

## 支持平台

| 平台 | 产物 | 说明 |
| --- | --- | --- |
| Windows | `GitHubDesktop2Chinese.exe`（x64） | 自动探测注册表安装目录；界面文案以 Windows 版 GitHub Desktop 为基准 |
| macOS | 通用二进制（Intel + Apple Silicon） | 静态链接 OpenSSL，自包含；安装目录需手动输入或用 `-g` 指定 |
| Linux | x64 可执行文件 | 依赖系统 OpenSSL（`libssl-dev`）；安装目录需手动输入或用 `-g` 指定 |

> 仅支持 64 位构建与运行；32 位版本已停止提供（v1.2.6 起）。
