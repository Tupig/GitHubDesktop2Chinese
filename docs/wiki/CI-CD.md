# CI / CD

仓库只有**一个工作流**：[`.github/workflows/ghdesktop2chinese.yml`](../../.github/workflows/ghdesktop2chinese.yml)。大型脚本全部外置于 `.github/scripts/`（数据质量校验 / 工具自检 / 维护报告），YAML 内只保留编排逻辑。

## 触发方式

| 事件 | 条件 | 说明 |
| --- | --- | --- |
| `push` | `main` 分支或 `v*` tag | push main 有构建相关变更时自动发布；push tag 始终发布 |
| `pull_request` | 目标为 `main` | 跑构建 + 质量门，不发布 |
| `schedule` | 每日 UTC 02:00（北京时间 10:00） | 完整链路：构建 → 检查 → 维护 → （有变更时）发布 |
| `workflow_dispatch` | 手动 | 输入 `type` + 可选 `version`，见下表 |

手动触发（`workflow_dispatch`）的 `type` 与执行范围：

| type | 执行范围 |
| --- | --- |
| `auto` | 完整链路：构建 → 检查 → 维护 → 发布（有变更时） |
| `build` | 仅构建多平台产物 |
| `release` | 构建 + 发布（仅 main 分支可用；CodeQL 与失效检测旁路执行，不阻塞发布） |
| `security` | 质量校验 + 工具自检 + CodeQL |
| `maintain` | 仅自动维护（失效检测 + 候选提取 + Issue） |

`version` 为可选覆盖：**留空即自动升级补丁号**（如 `1.2.24 → 1.2.25`）；填写时不带 `v` 前缀（如 `1.2.5`），必须为 `x.y.z` 纯数字，否则 CI 在版本号校验处直接失败；仅对 build / auto / release 的产物命名与发布生效。

并发控制：同一 ref 的重复触发会取消进行中的旧运行（PR 场景 `cancel-in-progress`），避免排队堆积。

权限收敛：`contents: read`、`issues: write`、`security-events: write`，仅声明所需。

## Job 矩阵

| Job | 触发 | 内容 |
| --- | --- | --- |
| `version`（变更检测） | push main / tag / 定时 / 手动 | 以最新 `v*` tag 为基准，检测 `json/`、`src/`、`third_party/`、`CMakeLists.txt`、`CMakePresets.json` 的实际变更，输出 `changed` 供发布环节判断 |
| `build` | PR / push / tag / 定时 / 手动 auto、build、release | **三目标构建 + 产物功能测试**：Windows x64、macOS 通用二进制（Intel + Apple Silicon，静态链接 OpenSSL 自包含）、Linux x64 |
| `json-quality` | PR / push / tag / 定时 / 手动 auto、security、release | `localization.json` 质量门：正则合法性、结构完整性、占位符检查、`std::regex` 不兼容语法黑名单、ReDoS 启发式、译文问句全角风格 |
| `tools-test` | PR / push / tag / 定时 / 手动 auto、security、maintain、release | 自动维护工具语法检查 + 单元测试 |
| `codeql` | PR（main）/ push main / 手动 auto、security | C/C++ 安全扫描 |
| `auto-maintain` | push main / 每日定时 / 手动 auto、maintain | 失效检测 + 候选提取，自动创建/关闭带 `auto-maintain` 标签的 Issue（详见[自动维护工具](自动维护工具.md#ci-自动维护)） |
| `release` | push main（有变更）/ tag `v*` / 定时（有变更）/ 手动 auto（main，有变更）、release（main） | 发布，见下节 |

## 发布规则

**版本号**：

- 自动模式：补丁号 +1（`1.2.24 → 1.2.25`）；
- 手动 `version` 输入：覆盖为指定 `x.y.z`；
- 版本号格式校验失败会在发布前直接 fail，不产生半成品版本。

**条件发布**：

- push `main`、定时、手动 `auto`：**仅当变更检测发现上述路径有实际变更时**才发布，无变更自动跳过（避免空版本）；
- 推送 `v*` tag 与手动 `type=release`：**始终发布**。

**发布内容**：

- 多平台产物：`GitHubDesktop2Chinese.exe`（Windows x64）、macOS 通用二进制、Linux x64；
- `localization.json`（用户可单独下载映射）；
- 每个产物的**构建溯源证明**（SLSA provenance），可用 `gh attestation verify <文件> --repo Tupig/GitHubDesktop2Chinese` 校验；
- Release 说明：从 [`docs/ReleaseBody.md`](../ReleaseBody.md) 提取**行尾含全角版本标记**（如 `（v1.2.25）`）的变更行拼接，再附静态的「### 程序说明」段落。未标注或版本号未命中的行不会出现（显示「未找到变更记录」提示），也**不阻断**发布。

**发布前置门禁**：`release` job 强制依赖 json-quality 与 tools-test 通过——质量门挂了就不会发版；Release 创建失败有自动自愈重试。

## 质量门明细（json-quality）

PR 与发布前对 `localization.json` 做静态校验（脚本：`.github/scripts/check-localization.py`）：

1. **结构与类型**：顶层对象、必需键、数组元素类型逐项校验；
2. **正则合法性**：每条查找正则必须可编译；
3. **`std::regex` 兼容黑名单**：后行断言、命名捕获组、`\p{…}`、内联 flag 等 JS 合法但 C++ 运行时抛错的语法直接报错；
4. **ReDoS 启发式**：灾难性回溯模式拦截；
5. **占位符检查**：`#{n}` 与捕获组 `$n` 使用合法性；
6. **译文风格**：问句全角 `？`、译文末尾空格；
7. **条目顺序**：「短含长」相对顺序告警。

## 对贡献者的意义

- PR 阶段会自动跑质量门与工具自检，本地过了 CI 一般也过；
- 合并到 main 后，只要改了 `json/`、`src/`、`third_party/`、`CMakeLists.txt`、`CMakePresets.json` 任一路径，**不需要手动发版**——下次 push / 每日定时会自动出补丁版本；
- 想让自己的变更行出现在 Release 说明里，记得按[贡献指南](贡献指南.md#4-发布说明docsreleasebodymd)在 `docs/ReleaseBody.md` 标注全角版本标记。
