# 安全政策

## 支持的版本

仅**最新发布版本**（自 `v1.2.30` 起）接收安全修复；更早版本不再维护，请先升级。

| 版本 | 支持 |
| --- | --- |
| `v1.2.30` 及以后 | ✅ |
| 更早版本 | ❌ |

## 报告漏洞

请**不要**通过公开 Issue 报告安全漏洞，使用 GitHub 私密漏洞报告（Private Vulnerability Reporting）：

- 仓库页 **Security → Report a vulnerability**
- 或直接访问：<https://github.com/Tupig/GitHubDesktop2Chinese/security/advisories/new>

报告建议包含：问题描述、复现步骤、影响范围（版本 / 平台）、可能的修复建议（可选）。

## 响应约定

- 一般 7 天内确认收到并给出初步评估；修复时间视严重程度而定
- 修复发布前请勿公开披露；发布后会在 Release 说明中致谢（如需匿名请在报告中注明）

## 安全设计

- **网络传输**：全部 HTTPS 并启用服务器证书校验（Windows 加载系统根证书存储；macOS 使用系统 `/etc/ssl/cert.pem`；Linux 探测系统 CA 包）
- **自更新**：按发布资产声明的 SHA256 校验下载完整性；文件替换采用原子写入并保留备份
- **供应链**
  - 第三方 Actions 全部按 commit SHA 固定（版本升级随上游手动执行；依赖漏洞由 Dependabot 告警提示）
  - Windows 的 OpenSSL 静态库由 CMake 从固定 URL 下载并校验 SHA256；macOS 静态库由 CI 基于上游 OpenSSL 3.x 源码构建
  - 发布产物附带构建溯源证明（SLSA provenance），可校验：

    ```bash
    gh attestation verify <下载的文件> --repo Tupig/GitHubDesktop2Chinese
    ```

- **代码与数据扫描**：CodeQL（C/C++，PR 与 main 推送）；`localization.json` 质量门（非法/危险正则黑名单、ReDoS 启发式、结构与占位符校验）；仓库已启用 Secret Scanning 与 Push Protection、Dependabot 漏洞告警与私密漏洞报告；`.gitignore` 防御性排除凭据/密钥类文件（`.env*`/`*.pem`/`*.key` 等）

## 范围说明

本程序按设计会修改本机 GitHub Desktop 的 `main.js` / `renderer.js`（汉化）并保留 `.bak` 备份，该行为属预期功能而非漏洞。漏洞范围包括但不限于：任意代码执行、路径穿越、更新流程被劫持、敏感信息泄露等。
