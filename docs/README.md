# 文档索引

**简体中文** | [English](#documentation-index-english)

本目录集中存放仓库文档。各文件职责与归类依据如下：

| 文件 | 职责 | 归类依据 |
| --- | --- | --- |
| `CONTRIBUTING.md` | 汉化贡献指南（规则 / 本地验证 / 发布说明约定） | GitHub 社区文件识别位置（根 / `.github/` / `docs/` 三选一），勿移动 |
| `ReleaseBody.md` | Release 说明模板（CI 按全角版本标记提取变更行） | 发布链路唯一真值来源（workflow `grep/awk` 直接引用），勿移动 |
| `wiki/` | GitHub Wiki 发布源（13 页：安装 / 使用 / 原理 / 贡献 / CI-CD 等） | 与主文档分层：主文档面向仓库读者，wiki 面向产品用户 |
| `README.md`（本文件） | 文档索引与归类说明 | — |

## 与其他文档的关系

- 根目录 `README.md` / `SECURITY.md` 为入口文档（平台约定位置）；本目录与 `wiki/` 为二级展开
- 根 README 与 `wiki/` 之间已建立交叉链接（如「贡献指南」「正则表达式指南」）

## wiki 维护约定

- `wiki/` 内容为 GitHub Wiki 的发布源：更新后同步到 Wiki 仓库（页面标题即文件名；`_Sidebar.md` / `_Footer.md` 为 Wiki 布局文件）
- **落地页文件名固定为 `Home.md`**：GitHub Wiki 的 `/wiki` 地址固定指向 Home 页面，改名或删除会使 `/wiki` 变为空白页面列表（已实测）；中文呈现通过 `_Sidebar.md` 的「首页」入口文案与页面内 H1 标题实现
- wiki 页与主文档出现内容重叠时（如命令行参数、常见问题），**以根目录文档为事实来源**，wiki 页保持摘要并链接回主文档

## 冻结路径清单（移动会破坏功能）

| 路径 | 原因 |
| --- | --- |
| `json/localization.json` | 已发布旧版程序内置其远程 URL（`raw.githubusercontent.com/.../main/json/localization.json`），移动即破坏历史版本 |
| `.github/workflows/` | GitHub Actions 仅识别该目录 |
| `docs/ReleaseBody.md` | CI 发布链路 `grep/awk` 真值来源 |
| `docs/CONTRIBUTING.md` | GitHub 社区文件识别位置 |
| `src/app.manifest`、`third_party/openssl/**` | CMake 链接 / 清单参数精确引用 |
| `CMakeLists.txt` / `CMakePresets.json` | 工具链约定位置 + CI 变更检测白名单 |

## 脚本位置

- `tools/auto-maintain/` — 数据维护工具（本地与 CI 共用；含共享规则 `regex-blacklist.json`）
- `tools/ci/` — CI 脚本（数据质量校验 / 工具自检 / Markdown 链接检查 / 冒烟测试 / 维护报告）

---

# Documentation Index (English)

[简体中文](#文档索引) | **English**

This directory holds the repository's documentation. Each file's role and placement rationale:

| File | Role | Placement rationale |
| --- | --- | --- |
| `CONTRIBUTING.md` | Localization contribution guide (rules / local verification / release-notes conventions) | A GitHub community-file location (one of root / `.github/` / `docs/`) — do not move |
| `ReleaseBody.md` | Release-notes template (CI extracts change lines by full-width version markers) | The single source of truth for the release pipeline (referenced directly by the workflow's `grep/awk`) — do not move |
| `wiki/` | GitHub Wiki publishing source (13 pages: install / usage / internals / contribution / CI-CD, etc.) | Layered apart from the main docs: main docs serve repository readers, the wiki serves product users |
| `README.md` (this file) | Documentation index and placement notes | — |

## Relationship to Other Documentation

- The root `README.md` / `SECURITY.md` are entry-point documents (platform-conventional locations); this directory and `wiki/` are the second level
- Cross-links are in place between the root README and `wiki/` (e.g. the contribution guide and the regex guide)

## Wiki Maintenance Conventions

- `wiki/` content is the publishing source for the GitHub Wiki: after updating, sync to the Wiki repository (the page title is the file name; `_Sidebar.md` / `_Footer.md` are the Wiki layout files)
- **The landing page file name is fixed as `Home.md`**: the GitHub Wiki `/wiki` URL always points to the Home page — renaming or deleting it leaves `/wiki` as a blank page list (verified in practice); the Chinese presentation is achieved via the「首页」entry text in `_Sidebar.md` and the H1 title inside the page
- When wiki pages overlap with the main docs (e.g. command-line options, FAQ), **the root-level documents are the source of truth**; wiki pages stay summary-level and link back to the main docs

## Frozen Paths (moving these breaks functionality)

| Path | Reason |
| --- | --- |
| `json/localization.json` | Released older builds embed its remote URL (`raw.githubusercontent.com/.../main/json/localization.json`); moving it breaks historical versions |
| `.github/workflows/` | GitHub Actions only recognizes this directory |
| `docs/ReleaseBody.md` | Source of truth for the CI release pipeline's `grep/awk` |
| `docs/CONTRIBUTING.md` | GitHub community-file location |
| `src/app.manifest`, `third_party/openssl/**` | Referenced precisely by CMake link / manifest parameters |
| `CMakeLists.txt` / `CMakePresets.json` | Toolchain-conventional locations + the CI change-detection whitelist |
