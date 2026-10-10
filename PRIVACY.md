# Privacy Policy / 隐私政策

> This document is bilingual: English first, Chinese below.

---

## Privacy Policy

**Last updated: 2026-10-10**

### Overview

GitHubDesktop2Chinese is a local tool that translates the GitHub Desktop user interface into Chinese by applying regex-based text replacements to GitHub Desktop's application files on your machine.

**We do not collect, transmit, or share any personal data.**

### Data collection

None. The application:

- contains **no telemetry, analytics, or crash reporting**;
- has **no user accounts, registration, or login**;
- does **not track** usage or behavior;
- does not read files other than the GitHub Desktop application files it modifies (and the `.bak` backups it creates).

### Network access

The application makes outbound requests **only to download content** — it never uploads user data:

- fetching the latest `localization.json` mapping file from GitHub (`raw.githubusercontent.com` / GitHub Releases);
- checking for and downloading application updates from GitHub Releases.

These requests are subject to [GitHub's own privacy statement](https://docs.github.com/en/site-policy/privacy-policies/github-privacy-statement).

### Local data

All files the tool creates or modifies stay on your machine:

- translations applied to GitHub Desktop's `main.js` / `renderer.js`;
- `.bak` backups of the original files (restorable at any time via `GitHubDesktop2Chinese.exe --rollback`).

Deleting the executable removes the program entirely; no residues are left in the system (no registry entries beyond GitHub Desktop's own, no services, no background processes).

### Changes to this policy

Any changes will be published in this repository.

### Contact

Open an issue at <https://github.com/Tupig/GitHubDesktop2Chinese/issues>.

---

## 隐私政策

**最后更新：2026-10-10**

### 概述

GitHubDesktop2Chinese 是一个本地工具，通过对 GitHub Desktop 的应用文件执行正则文本替换，将其界面翻译为中文。

**我们不收集、传输或共享任何个人数据。**

### 数据收集

无。本程序：

- **不含任何遥测、统计或崩溃上报**；
- **没有用户账号、注册或登录**；
- **不追踪**使用情况或行为；
- 除被修改的 GitHub Desktop 应用文件（及其创建的 `.bak` 备份）外，不读取其他文件。

### 网络访问

程序仅发起**下载类**网络请求——从不上传用户数据：

- 从 GitHub（`raw.githubusercontent.com` / GitHub Releases）拉取最新 `localization.json` 映射文件；
- 从 GitHub Releases 检查并下载程序更新。

上述请求受 [GitHub 隐私声明](https://docs.github.com/en/site-policy/privacy-policies/github-privacy-statement)约束。

### 本地数据

工具创建或修改的所有文件均保留在你的机器上：

- 应用到 GitHub Desktop `main.js` / `renderer.js` 的汉化内容；
- 原始文件的 `.bak` 备份（随时可通过 `GitHubDesktop2Chinese.exe --rollback` 还原）。

删除可执行文件即完全移除本程序；系统无残留（除 GitHub Desktop 自身的注册表项外不写注册表、无服务、无后台进程）。

### 政策变更

任何变更将发布于本仓库。

### 联系方式

请在 <https://github.com/Tupig/GitHubDesktop2Chinese/issues> 提交 Issue。
