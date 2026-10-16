# 安全政策

**简体中文** | [English](#security-policy-english)

## 支持的版本

仅**最新发布版本**（自 `v0.0.1` 起）接收安全修复；更早版本不再维护，请先升级。

| 版本 | 支持 |
| --- | --- |
| `v0.0.1` 及以后 | ✅ |
| 更早版本 | ❌ |

## 报告漏洞

请**不要**通过公开 Issue 报告安全漏洞，使用 GitHub 私密漏洞报告（Private Vulnerability Reporting）：

- 仓库页 **Security → Report a vulnerability**
- 或直接访问：<https://github.com/Tupig/TuPigGHD2Chinese/security/advisories/new>

报告建议包含：问题描述、复现步骤、影响范围（版本 / 平台）、可能的修复建议（可选）。

## 响应约定

- 一般 7 天内确认收到并给出初步评估；修复时间视严重程度而定
- 修复发布前请勿公开披露；发布后会在 Release 说明中致谢（如需匿名请在报告中注明）

## 安全设计

- **网络传输**：全部 HTTPS 并启用服务器证书校验（Windows 加载系统根证书存储；macOS 使用系统 `/etc/ssl/cert.pem`；Linux 探测系统 CA 包）
- **自更新**：按发布资产声明的 SHA256 校验下载完整性；文件替换采用原子写入并保留备份
- **供应链**
  - 第三方 Actions 全部按 commit SHA 固定（版本升级随上游手动执行；依赖漏洞由 Dependabot 告警提示）
  - Windows 的 OpenSSL 静态库与头文件直接随仓库提交（`third_party/openssl/`，由上游源码构建，版本记录与构建流程见 [`third_party/README.md`](third_party/README.md)）；macOS 静态库由 CI 基于上游 OpenSSL 3.x 源码构建
  - 发布产物附带构建溯源证明（SLSA provenance），可校验：

    ```bash
    gh attestation verify <下载的文件> --repo Tupig/TuPigGHD2Chinese
    ```

  - Windows 发布产物支持 [SignPath](https://signpath.org) 自动代码签名；签名凭据未配置时自动跳过（产物不签名，发布不受阻——当前即此状态：SignPath Foundation 申请未获批，集成保持休眠）；启用签名后对签名产物重新生成构建溯源证明
- **代码与数据扫描**：CodeQL（C/C++，PR 与 main 推送）；`localization.json` 质量门（非法/危险正则黑名单、ReDoS 启发式、结构与占位符校验）；仓库已启用 Secret Scanning 与 Push Protection、Dependabot 漏洞告警与私密漏洞报告；`.gitignore` 防御性排除凭据/密钥类文件（`.env*`/`*.pem`/`*.key` 等）

## 范围说明

本程序按设计会修改本机 GitHub Desktop 的 `main.js` / `renderer.js`（汉化）并保留 `.bak` 备份，该行为属预期功能而非漏洞。漏洞范围包括但不限于：任意代码执行、路径穿越、更新流程被劫持、敏感信息泄露等。

---

# Security Policy (English)

[简体中文](#安全政策) | **English**

## Supported Versions

Only the **latest release** (since `v0.0.1`) receives security fixes; earlier versions are no longer maintained — please upgrade first.

| Version | Supported |
| --- | --- |
| `v0.0.1` and later | ✅ |
| Earlier versions | ❌ |

## Reporting a Vulnerability

Please do **not** report security vulnerabilities through public issues. Use GitHub's Private Vulnerability Reporting instead:

- Repository page: **Security → Report a vulnerability**
- Or go directly to: <https://github.com/Tupig/TuPigGHD2Chinese/security/advisories/new>

Reports should ideally include: a description of the issue, reproduction steps, the affected scope (version / platform), and, optionally, a suggested fix.

## Response Expectations

- We generally acknowledge receipt and provide an initial assessment within 7 days; fix timelines depend on severity
- Please do not disclose the issue publicly before a fix is released; contributors are credited in the release notes (mention in your report if you prefer to remain anonymous)

## Security Design

- **Network transport**: HTTPS everywhere with server certificate validation enabled (Windows loads the system root certificate store; macOS uses the system `/etc/ssl/cert.pem`; Linux probes the system CA bundle)
- **Self-update**: download integrity is verified against the SHA256 declared by the release asset; file replacement is atomic and keeps backups
- **Supply chain**
  - Third-party Actions are all pinned by commit SHA (upgrades are performed manually; dependency vulnerabilities are flagged by Dependabot alerts)
  - The Windows OpenSSL static libraries and headers are committed directly to the repository (`third_party/openssl/`, built from upstream sources — see [`third_party/README.md`](third_party/README.md) for version records and the build process); macOS static libraries are built by CI from upstream OpenSSL 3.x sources
  - Release artifacts carry build provenance attestations (SLSA provenance) that can be verified with:

    ```bash
    gh attestation verify <downloaded file> --repo Tupig/TuPigGHD2Chinese
    ```

  - Windows release artifacts support automatic code signing via [SignPath](https://signpath.org); when signing credentials are not configured, signing is skipped automatically (artifacts stay unsigned and releases are never blocked — the current state: the SignPath Foundation application was not approved and the integration remains dormant); once signing is enabled, the build provenance attestation is regenerated for the signed artifact
- **Code and data scanning**: CodeQL (C/C++, on PRs and pushes to main); a `localization.json` quality gate (denylist of illegal/dangerous regexes, ReDoS heuristics, structure and placeholder checks); the repository has Secret Scanning, Push Protection, Dependabot alerts, and private vulnerability reporting enabled; `.gitignore` defensively excludes credential/key files (`.env*` / `*.pem` / `*.key`, etc.)

## Scope

By design, this program modifies the local GitHub Desktop `main.js` / `renderer.js` (localization) and keeps `.bak` backups; that behavior is an intended feature, not a vulnerability. Vulnerabilities include, but are not limited to: arbitrary code execution, path traversal, hijacking of the update flow, and leakage of sensitive information.
