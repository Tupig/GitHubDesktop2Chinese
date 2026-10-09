# 文档索引

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
- `tools/ci/` — CI 脚本（数据质量校验 / 工具自检 / 维护报告）
