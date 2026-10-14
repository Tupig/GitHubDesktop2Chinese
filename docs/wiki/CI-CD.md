# CI / CD

**简体中文** | [English](#ci--cd-english)

仓库只有**一个工作流** [`.github/workflows/ghdesktop2chinese.yml`](../../.github/workflows/ghdesktop2chinese.yml)（构建 / 检查 / 维护 / Wiki 同步 / OpenSSL 产库 / 发布）。大型脚本全部外置于 `tools/ci/`（数据质量校验 / 编码检查 / 工具自检 / Markdown 链接检查 / 单工作流规范校验 / 冒烟测试 / 维护报告 / Wiki 同步），YAML 内只保留编排逻辑。Wiki 同步见[下文](#wiki-自动同步)。

## 触发方式

| 事件 | 条件 | 说明 |
| --- | --- | --- |
| `push` | `main` 分支或 `v*` tag | push main 有构建相关变更时自动发布；push tag 始终发布 |
| `pull_request` | 目标为 `main` | 跑构建 + 质量门，不发布 |
| `schedule` | 每日 UTC 01:23（北京时间 09:23；分钟避开整点，防高负载延迟/丢弃） | 完整链路：构建（无差异时跳过）→ 检查 → 维护 → （有变更时）发布 |
| `workflow_dispatch` | 手动 | 输入 `type` + 可选 `version`，见下表 |

手动触发（`workflow_dispatch`）的 `type` 与执行范围：

| type | 执行范围 |
| --- | --- |
| `auto` | 完整链路：构建 → 检查 → 维护 → 发布（有变更时） |
| `build` | 仅构建多平台产物 |
| `release` | 构建 + 发布（仅 main 分支可用；CodeQL 与失效检测旁路执行，不阻塞发布） |
| `security` | 质量校验 + 工具自检 + CodeQL |
| `maintain` | 仅自动维护（失效检测 + 候选提取 + Issue） |
| `vendor` | 仅产库 OpenSSL 静态库（三平台 artifact，版本由 `openssl-version` 指定） |

`version` 为可选覆盖：**留空即自动递增版本**（如 `0.0.1 → 0.0.2`，patch 满 100 进位 minor 并重置为 1）；填写时不带 `v` 前缀（如 `0.0.2`），必须为 `x.y.z` 纯数字，否则 CI 在版本号校验处直接失败；仅对 build / auto / release 的产物命名与发布生效。`openssl-version` 仅 `type=vendor` 生效：产库 OpenSSL 版本（不带 `v` 前缀，须为 `3.x.y` LTS），留空默认 `3.5.9`。

并发控制：同一 ref 的重复触发会取消进行中的旧运行（PR 场景 `cancel-in-progress`），避免排队堆积。

权限收敛：`contents: read`、`issues: write`、`security-events: write`，仅声明所需。

## Job 矩阵

Job 依赖关系：`json-quality` / `encoding-check` / `tools-test` / `codeql` / `auto-maintain` / `sync-wiki` / `build` / `build-skip-notice` / `vendor-*` 均 ← `version`；`release` ← `version + build + json-quality + tools-test + encoding-check`。六个检查/维护类任务（质量校验、编码检查、工具自检、CodeQL、失效检测、Wiki 同步）与构建矩阵同在解析版本号之后执行，定义顺序位于构建产物之前。**CodeQL 与 auto-maintain 是旁路校验**——手动 `release` 时也会执行（发现问题出 Issue / 告警），但**不阻塞发布**。

| Job | 触发 | 内容 |
| --- | --- | --- |
| `version`（变更检测） | 全部触发场景（PR / push / tag / 定时 / 手动） | 以最新 `v*` tag 为基准，检测 `json/`、`src/`、`third_party/`、`CMakeLists.txt`、`CMakePresets.json` 的实际变更（排除纯文档 `third_party/README.md`），输出 `changed` 供发布环节判断；并与上次成功构建对比输出 `build_changed`（见[构建差异检测](#构建差异检测构建跳过)） |
| `json-quality` | PR / push / tag / 定时 / 手动 auto、security、release | `localization.json` 质量门：正则合法性、结构完整性、占位符检查、`std::regex` 不兼容语法黑名单、ReDoS 启发式、译文问句全角风格 |
| `encoding-check` | PR / push / tag / 定时 / 手动 auto、security、release | 源码编码格式门：全量扫描 git 跟踪文本文件要求 UTF-8；GBK / 非 UTF-8 与意外 BOM 以 `::error` 清单 + 非零退出码失败；第三方依赖排除 |
| `tools-test` | PR / push / tag / 定时 / 手动 auto、security、maintain、release | 自动维护工具语法检查 + 单元测试 + Markdown 链接检查（`tools/ci/md-check.py`）+ 单工作流规范校验（`tools/ci/check-workflow.py`，W0-W5） |
| `codeql` | PR（仅同仓分支，fork PR 不跑——其令牌无 security-events 写权限，上传必失败）/ push main / 手动 auto、security、release | C/C++ 安全扫描（不含定时，节省资源；运行于 Windows runner） |
| `auto-maintain` | push main / 每日定时 / 手动 auto、maintain、release | 失效检测 + 候选提取，自动创建/关闭带 `auto-maintain` 标签的 Issue（详见[自动维护工具](自动维护工具.md#ci-自动维护)） |
| `sync-wiki` | push main / 手动 auto、release | 检测 `docs/wiki/**` 变更后将文档镜像同步到 GitHub Wiki（见[Wiki 自动同步](#wiki-自动同步)） |
| `build` | PR / push / tag / 定时 / 手动 auto、build、release；无差异的定时 / push main 跳过（见[构建差异检测](#构建差异检测构建跳过)） | **四目标构建矩阵 + 产物功能测试**：Windows x64、macOS x64 / arm64（单架构包，静态链接 OpenSSL + strip/LTO 裁剪）、Linux x64；`fail-fast` 关闭，单平台失败不取消其余平台 |
| `build-skip-notice` | 仅无差异的定时 / push main | 在 Run 摘要 / notice / 日志中输出跳过原因（skipped job 自身无日志，故设独立 job 说明） |
| `vendor-linux` / `vendor-windows` / `vendor-macos` | PR 打 `vendor-openssl` 标签 / 手动 `vendor` | 产库 OpenSSL 静态库（三平台 artifact，保留 7 天），供维护者下载后提交 `third_party/`，构建期零下载零编译 |
| `release` | push main（有变更）/ tag `v*` / 定时（有变更）/ 手动 auto（main，有变更）、release（main） | 发布，见下节 |

## 发布规则

**版本号**：

- 自动模式：patch +1（`0.0.1 → 0.0.2`）；patch 满 100 进位 minor 并重置为 1（`0.0.100 → 0.1.1`），minor 满 100 进位 major 并重置为 0（`0.100.100 → 1.0.1`）；无 tag 时自 `v0.0.0` 起步；
- 手动 `version` 输入：覆盖为指定 `x.y.z`；
- 版本号格式校验失败会在发布前直接 fail，不产生半成品版本；
- **版本漂移检查**：发布前按进位规则核对最新 tag 是否为待发版本的前驱（`v0.0.1` 无前驱；`v0.1.1` 的前驱为 `v0.0.100`）。并发排队等场景导致不一致时仅输出 `::warning` 告警并记录实际对应关系，**不阻断**发布。

**条件发布**：

- push `main`、定时、手动 `auto`：**仅当变更检测发现上述路径有实际变更时**才发布，无变更自动跳过（避免空版本）；
- 推送 `v*` tag 与手动 `type=release`：**始终发布**。

**发布内容**：

- 多平台产物：`GitHubDesktop2Chinese.exe`（Windows x64）、`GitHubDesktop2Chinese-macos-x64`、`GitHubDesktop2Chinese-macos-arm64`、`GitHubDesktop2Chinese-linux-x64`；
- `localization.json`（用户可单独下载映射）；
- 每个产物的**构建溯源证明**（SLSA provenance），可用 `gh attestation verify <文件> --repo Tupig/GitHubDesktop2Chinese` 校验；
- Windows 产物的代码签名：配置 SignPath 凭据后自动签名（当前休眠——Foundation 申请未获批，见[代码签名](#代码签名signpath)）；
- Release 说明：优先从 [`docs/ReleaseBody.md`](../ReleaseBody.md) 提取**行尾含全角版本标记**（如 `（v0.0.2）`）的变更行拼接（变更行中英成对标注时一并纳入）；当前版本未预置条目时，发布流程**自动提取上一发布版本以来的提交标题**生成变更条目，写入 `docs/ReleaseBody.md` 的 Changelog 段后纳入说明（Release 页会标注「自动提取」）。两者皆未命中才显示「未找到变更记录」提示，均**不阻断**发布。最后附静态的「### 程序说明」段落。

**发布前置门禁**：`release` job 强制依赖 json-quality、encoding-check 与 tools-test 通过——质量门挂了就不会发版；CodeQL 与失效检测在手动 release 时为**旁路执行**（发现问题时告警/开 Issue，不阻塞发布）；Release 创建失败有自动自愈重试。

**发布边界**：发布类（`auto` / `release`）仅限 `main` 分支触发，防止特性分支误发布；其他分支可跑 build / security / maintain 做排查。

## 构建差异检测（构建跳过）

无意义的重复构建只消耗 runner 资源，不产生任何价值：

- **评估范围**：仅「无待发布变更」（`changed=false`）的**定时**与 **push main** 运行；
- **对比基准**：main 分支最近一次成功 run 的 head SHA（API 查询失败时保守回退为构建，fail-open）；
- **对比路径**：与变更检测同一份构建白名单（`json/`、`src/`、`third_party/`、`CMakeLists.txt`、`CMakePresets.json`，排除 `third_party/README.md`）；
- **无差异** → `build_changed=false`，跳过整个四平台构建矩阵，由独立的 `build-skip-notice` job 在 Run 摘要 / notice / 日志中输出原因（skipped job 自身无日志）；
- **恒构建保证**：`changed=true` 时 `build_changed` 恒为 `true`（强制构建，发布所需产物永远存在）；PR（质量门每次须验证）、tag（连着发布）、手动触发（尊重用户意图）**始终构建**，不做跳过评估。

## Wiki 自动同步

文档页面（`docs/wiki/`）合入 `main` 后，由主工作流的 `sync-wiki` 任务自动镜像到 GitHub Wiki 标签页：

- **触发**：push `main` 或手动 `type=auto|release`；任务内先检测 `docs/wiki/**` 是否有变更，无变更自动跳过（手动触发视为有变更，可作初始化 / 修复 / 验证兜底）；
- **同步语义**：镜像同步——`docs/wiki/` 为唯一真值，Wiki 端多余的页面会被**删除**（实现见 [`tools/ci/sync-wiki.sh`](../../tools/ci/sync-wiki.sh)）；
- **凭据**：内置 `GITHUB_TOKEN`（`contents: write` 即可推送同仓库 Wiki，无需 PAT）；
- **并发**：与主工作流共享全局 `concurrency` 组（同一 ref 串行），不与其他运行交叉执行。

## 代码签名（SignPath）

Windows 产物（`GitHubDesktop2Chinese.exe`）的发布前代码签名流程已内置，当前处于**休眠**状态（2026-10 更新）。

**现状**：SignPath Foundation 申请未获批准——该计划要求项目具备一定的公开可见度信号（社区采用度、独立引用、外部讨论等），本项目暂未达标；凭据未配置，签名步骤自动跳过，**当前发布产物未签名**，发布不受影响。产物仍带 SLSA 构建溯源证明（不依赖签名），Release 说明中的 SignPath 声明仅在签名实际发生时输出。

- **启用条件**：仓库 Secrets `SIGNPATH_API_TOKEN` 与 Variables `SIGNPATH_ORGANIZATION_ID` 同时配置才签名；任一缺失自动跳过，产物保持未签名，发布流程不受影响；
- **可覆盖项**：Variables `SIGNPATH_PROJECT_SLUG`（默认 `GitHubDesktop2Chinese`）、`SIGNPATH_SIGNING_POLICY_SLUG`（默认 `release-signing`）；
- **流程**（release 任务内）：下载构建产物 → 单独上传待签名 exe（SignPath 要求签名对象先作为 GitHub artifact 存在）→ 提交签名请求并等待完成 → 签名产物替换原 exe → 对签名产物**重新生成构建溯源证明**（签名会改变文件内容，未签名版本的证明摘要不再匹配）；
- **后续启用路径**（维护者，二选一）：社区可见度提升后**重新申请** [SignPath Foundation](https://signpath.org/apply)（可再次审核），或转[付费订阅](https://docs.signpath.io/change-subscription)；获批后按下述一次性配置接入即可，无需改动代码；
- **SignPath 侧一次性配置**（维护者）：创建项目（slug 与仓库名一致；Artifact Configuration 引用 [`.signpath/artifact-configuration.xml`](../../.signpath/artifact-configuration.xml)；Trusted Build System 关联 GitHub.com 本仓库）→ 创建 `release-signing` 签名策略 → 生成 API Token 与组织 ID 分别存入仓库 Secrets / Variables。

## 质量门明细（json-quality）

PR 与发布前对 `localization.json` 做静态校验（脚本：`tools/ci/check-localization.py`）：

1. **结构与类型**：顶层对象、必需键、数组元素类型逐项校验；
2. **正则合法性**：每条查找正则必须可编译；
3. **`std::regex` 兼容黑名单**：后行断言、命名捕获组、`\p{…}`、内联 flag 等 JS 合法但 C++ 运行时抛错的语法直接报错；黑名单维护在 [`tools/auto-maintain/regex-blacklist.json`](../../tools/auto-maintain/regex-blacklist.json)，CI 脚本与自动维护工具共用这一份单一事实来源；
4. **ReDoS 启发式**：灾难性回溯模式拦截；
5. **占位符检查**：`#{n}` 与捕获组 `$n` 使用合法性；
6. **译文风格**：问句全角 `？`、译文末尾空格；
7. **条目顺序**：「短含长」相对顺序告警。

## 质量门明细（encoding-check）

PR 与发布前对源码编码做全量扫描（脚本：`tools/ci/check-encoding.py`）：

1. **扫描范围**：全部 git 跟踪的文本文件（构建产物未被跟踪，天然排除）；
2. **规范**：必须为 UTF-8（无 BOM）；
3. **违规判定**：GBK / 非 UTF-8 内容、意外 BOM → 以 `::error` 逐文件清单报告并以非零退出码使流程失败；
4. **排除项**：第三方依赖目录（编码不受本仓管控）；`smoke-test.ps1` 为 BOM 白名单例外（Windows PowerShell 5.1 兼容）。

**配置化（v2）**：检测参数不再硬编码在脚本里，[`tools/ci/encoding-check.json`](../../tools/ci/encoding-check.json) 是唯一事实来源，CI 显式以 `--config` 传入。三级合并优先级：内置默认（不传参时与 v1 行为一致）< 配置文件 < 命令行参数（`--exclude` / `--allow` 追加；`--strict` / `--continue-on-violation` 覆盖宽松开关）。配置键：

| 配置键 | 类型 | 默认 | 说明 |
| --- | --- | --- | --- |
| `allowed_encodings` | string[] | `["utf-8", "ascii"]` | 允许的编码白名单（检测标签小写） |
| `exclude_dirs` | string[] | `["third_party/"]` | 排除目录（正斜杠前缀匹配） |
| `bom_exceptions` | string[] | `["tools/ci/smoke-test.ps1"]` | 允许携带 UTF-8 BOM 的文件（丢失 BOM 仅告警） |
| `binary_extensions` | string[] | （见配置文件） | 读文件前直接跳过的二进制扩展名 |
| `sample_size` | int | `8192` | BOM / NUL 采样探测字节数 |
| `workers` | int | `0` | 并行线程数；`0` = 自动（`min(32, CPU×4)`） |
| `continue_on_violation` | bool | `false` | 宽松模式：违规降级为 `::warning` 且退出码 0 |

**宽松模式**：存量治理过渡期用——违规只出 `::warning` 不阻断流水线（`--continue-on-violation`，或配置 `continue_on_violation: true`）；本仓当前无存量违规，CI 保持严格模式。

**本地使用**：

```bash
python tools/ci/check-encoding.py --config tools/ci/encoding-check.json   # 与 CI 完全一致
python tools/ci/check-encoding.py --exclude docs/legacy/                  # 临时追加排除
```

**常见问题**：

- **为什么强制 UTF-8 无 BOM？** JSON 带 BOM 违反 RFC 8259；`.sh` 带 BOM 破坏 shebang；GBK 内容会导致 Linux CI 乱码（详见 [#85 审计](https://github.com/Tupig/GitHubDesktop2Chinese/issues/85)）。
- **违规了怎么修？** 用编辑器或 `iconv -f GBK -t UTF-8 <文件>` 转为 UTF-8 无 BOM 后重新提交；不要靠加白名单绕过。
- **为什么 `smoke-test.ps1` 可以带 BOM？** 不带 BOM 时 Windows PowerShell 5.1 会把脚本按 ANSI 解析导致中文乱码；它是仓库唯一例外。
- **配置文件写错了会怎样？** JSON 语法错误 / 键类型不对 → `::error` + 退出码 2 直接失败；未知键 → `::warning` 提示后忽略。

## 对贡献者的意义

- PR 阶段会自动跑质量门与工具自检，本地过了 CI 一般也过；
- 合并到 main 后，只要改了 `json/`、`src/`、`third_party/`（不含纯文档 `third_party/README.md`）、`CMakeLists.txt`、`CMakePresets.json` 任一路径，**不需要手动发版**——下次 push / 每日定时会自动出补丁版本；
- 修改 `docs/wiki/` 并合入 main 后，GitHub Wiki 页面自动同步，无需手动维护两份文档；
- 想让自己的变更行出现在 Release 说明里，记得按[贡献指南](贡献指南.md#4-发布说明docsreleasebodymd)在 `docs/ReleaseBody.md` 标注全角版本标记。

---

# CI / CD (English)

[简体中文](#ci--cd) | **English**

The repository has a **single workflow**: [`.github/workflows/ghdesktop2chinese.yml`](../../.github/workflows/ghdesktop2chinese.yml) (build / checks / maintenance / Wiki sync / OpenSSL vendoring / release). Large scripts live outside the YAML in `tools/ci/` (data quality validation / encoding check / tool self-tests / Markdown link checks / single-workflow spec validation / smoke tests / maintenance reports / Wiki sync); the YAML only holds the orchestration. Wiki sync is covered [below](#automatic-wiki-sync).

## Triggers

| Event | Condition | Description |
| --- | --- | --- |
| `push` | `main` branch or `v*` tag | Push to main auto-releases when build-related changes exist; pushing a tag always releases |
| `pull_request` | targeting `main` | Runs build + quality gates, no release |
| `schedule` | Daily UTC 01:23 (Beijing 09:23; the minute avoids the top of the hour to prevent delay/dropping under heavy load) | Full pipeline: build (skipped when unchanged) → checks → maintenance → release (when changed) |
| `workflow_dispatch` | Manual | Inputs `type` + optional `version`, see the table below |

Execution scope per `type` for manual (`workflow_dispatch`) runs:

| type | Scope |
| --- | --- |
| `auto` | Full pipeline: build → checks → maintenance → release (when changed) |
| `build` | Build multi-platform artifacts only |
| `release` | Build + release (main branch only; CodeQL and invalid-entry detection run as bypasses and don't block the release) |
| `security` | Quality validation + tool self-tests + CodeQL |
| `maintain` | Maintenance only (invalid detection + candidate extraction + Issues) |
| `vendor` | OpenSSL static-library vendoring only (three-platform artifacts; version via `openssl-version`) |

`version` is an optional override: **leave it empty to auto-increment the version** (e.g. `0.0.1 → 0.0.2`; a full patch of 100 carries into minor, resetting it to 1); when provided, omit the `v` prefix (e.g. `0.0.2`) and it must be plain digits `x.y.z`, otherwise CI fails at version validation; it only affects artifact naming and releases for build / auto / release. `openssl-version` only takes effect for `type=vendor`: the OpenSSL version to vendor (no `v` prefix, must be `3.x.y` LTS); empty defaults to `3.5.9`.

Concurrency: repeated triggers for the same ref cancel older in-progress runs (`cancel-in-progress` in the PR scenario), avoiding queue pile-ups.

Scoped permissions: `contents: read`, `issues: write`, `security-events: write` — only what is needed.

## The Job Matrix

Job dependencies: `json-quality` / `encoding-check` / `tools-test` / `codeql` / `auto-maintain` / `sync-wiki` / `build` / `build-skip-notice` / `vendor-*` all ← `version`; `release` ← `version + build + json-quality + tools-test + encoding-check`. The six check/maintenance jobs (quality gate, encoding gate, tool self-test, CodeQL, invalid-entry detection, wiki sync) run after version parsing alongside the build matrix, ordered before the build jobs in the workflow definition. **CodeQL and auto-maintain are bypass checks** — they also run on manual `release` (opening Issues / warnings on findings) but **never block the release**.

| Job | Triggers | Contents |
| --- | --- | --- |
| `version` (change detection) | All trigger scenarios (PR / push / tag / schedule / manual) | Using the latest `v*` tag as the baseline, detects actual changes in `json/`, `src/`, `third_party/`, `CMakeLists.txt`, `CMakePresets.json` (excluding the docs-only `third_party/README.md`) and outputs `changed` for the release stage; also compares against the last successful build to output `build_changed` (see [Build Diff Detection](#build-diff-detection-build-skipping)) |
| `json-quality` | PR / push / tag / schedule / manual auto, security, release | The `localization.json` quality gate: regex validity, structural integrity, placeholder checks, the `std::regex` incompatible-syntax blacklist, ReDoS heuristics, full-width question style for translated questions |
| `encoding-check` | PR / push / tag / schedule / manual auto, security, release | Source encoding gate: full scan of git-tracked text files requiring UTF-8; GBK / non-UTF-8 content and unexpected BOMs fail with an explicit `::error` list; third-party dependencies excluded |
| `tools-test` | PR / push / tag / schedule / manual auto, security, maintain, release | Auto-maintain tool syntax check + unit tests + Markdown link check (`tools/ci/md-check.py`) + single-workflow spec validation (`tools/ci/check-workflow.py`, W0-W5) |
| `codeql` | PR (same-repo branches only — fork PRs don't run it: their token lacks security-events write permission, so upload would always fail) / push main / manual auto, security, release | C/C++ security scanning (excludes schedule to save resources; runs on a Windows runner) |
| `auto-maintain` | push main / daily schedule / manual auto, maintain, release | Invalid-entry detection + candidate extraction, automatically creating/closing Issues labeled `auto-maintain` (see [Auto-Maintain Tool](自动维护工具.md#ci-自动维护)) |
| `sync-wiki` | push main / manual auto, release | Mirrors `docs/wiki/**` to the GitHub Wiki after detecting changes (see [Automatic Wiki Sync](#automatic-wiki-sync)) |
| `build` | PR / push / tag / schedule / manual auto, build, release; skipped for unchanged schedule / push main runs (see [Build Diff Detection](#build-diff-detection-build-skipping)) | **Four-target build matrix + artifact functional tests**: Windows x64, macOS x64 / arm64 (single-arch packages, statically linked OpenSSL + strip/LTO trimming), Linux x64; `fail-fast` disabled so a single-platform failure doesn't cancel the others |
| `build-skip-notice` | Only unchanged schedule / push main runs | Posts the skip reason in the run summary / notices / logs (skipped jobs produce no logs of their own, hence the standalone job) |
| `vendor-linux` / `vendor-windows` / `vendor-macos` | PR labeled `vendor-openssl` / manual `vendor` | Vendor OpenSSL static libraries (three-platform artifacts, retained 7 days) for maintainers to download and commit under `third_party/`; zero downloads or compilation at build time |
| `release` | push main (with changes) / tag `v*` / schedule (with changes) / manual auto (main, with changes), release (main) | The release; see the next section |

## Release Rules

**Version numbers**:

- Auto mode: patch +1 (`0.0.1 → 0.0.2`); a full patch of 100 carries into minor resetting it to 1 (`0.0.100 → 0.1.1`), and a full minor of 100 carries into major resetting it to 0 (`0.100.100 → 1.0.1`); with no tags, numbering starts from `v0.0.0`;
- Manual `version` input: overrides with the given `x.y.z`;
- A malformed version fails before publishing — no half-baked versions;
- **Version drift check**: before publishing, verifies per the carry rule that the latest tag is the predecessor of the version being released (`v0.0.1` has none; the predecessor of `v0.1.1` is `v0.0.100`). On mismatch (e.g. queued concurrent runs) it only emits a `::warning` recording the actual correspondence and **does not block** the release.

**Conditional release**:

- push `main`, schedule, manual `auto`: releases **only when change detection found actual changes** in those paths; otherwise skipped automatically (avoiding empty versions);
- Pushing a `v*` tag and manual `type=release`: **always releases**.

**Release contents**:

- Multi-platform artifacts: `GitHubDesktop2Chinese.exe` (Windows x64), `GitHubDesktop2Chinese-macos-x64`, `GitHubDesktop2Chinese-macos-arm64`, `GitHubDesktop2Chinese-linux-x64`;
- `localization.json` (users can download the mapping separately);
- A **build provenance attestation** (SLSA provenance) for every artifact, verifiable with `gh attestation verify <file> --repo Tupig/GitHubDesktop2Chinese`;
- Code signing for the Windows artifact: signed automatically once SignPath credentials are configured (currently dormant — the Foundation application was not approved; see [Code Signing](#代码签名signpath));
- Release notes: change lines extracted from [`docs/ReleaseBody.md`](../ReleaseBody.md) whose **line ends carry the full-width version marker** (e.g. `（v0.0.2）`) are joined (paired Chinese/English changelog lines are included together); when no entry is pre-written for the current version, the release flow **auto-extracts commit subjects since the previous release**, writes them into the `docs/ReleaseBody.md` changelog section and includes them (marked "auto-extracted" on the release page). Only when both miss does the "no changelog found" notice appear — neither case **blocks** the release. The static "### 程序说明" section is appended last.

**Release gates**: the `release` job hard-depends on json-quality, encoding-check, and tools-test — a failed quality gate means no release; CodeQL and invalid-entry detection are **bypass checks** on manual releases (warnings/Issues on findings, no blocking); Release creation has automatic self-healing retries.

**Release boundary**: release-type runs (`auto` / `release`) can only be triggered on `main`, preventing accidental releases from feature branches; other branches can run build / security / maintain for troubleshooting.

## Build Diff Detection (Build Skipping)

Meaningless repeat builds only burn runner resources without producing any value:

- **Evaluated for**: **schedule** and **push main** runs, and only when there is **nothing pending release** (`changed=false`);
- **Comparison baseline**: the head SHA of the most recent successful run on the `main` branch (on API failure it conservatively falls back to building — fail-open);
- **Comparison paths**: the same build whitelist as change detection (`json/`, `src/`, `third_party/`, `CMakeLists.txt`, `CMakePresets.json`, excluding `third_party/README.md`);
- **No differences** → `build_changed=false`: the entire four-platform build matrix is skipped, and the standalone `build-skip-notice` job posts the reason in the run summary / notices / logs (skipped jobs produce no logs of their own);
- **Always-build guarantee**: when `changed=true`, `build_changed` is always `true` (forced build — the artifacts a release needs always exist); PRs (quality gates must be verified every time), tags (release follows immediately), and manual triggers (respecting user intent) **always build**, with no skip evaluation.

## Automatic Wiki Sync

After documentation pages (`docs/wiki/`) merge into `main`, the main workflow's `sync-wiki` job mirrors them to the GitHub Wiki tab automatically:

- **Triggers**: push to `main` or manual `type=auto|release`; the job first checks whether `docs/wiki/**` changed and skips automatically when nothing changed (manual runs always count as changed, serving as an initialization / repair / verification fallback);
- **Sync semantics**: mirror sync — `docs/wiki/` is the single source of truth; pages that no longer exist in the source are **deleted** on the Wiki (implementation: [`tools/ci/sync-wiki.sh`](../../tools/ci/sync-wiki.sh));
- **Credentials**: the built-in `GITHUB_TOKEN` (`contents: write` is enough to push the same repository's Wiki, no PAT needed);
- **Concurrency**: shares the workflow's global `concurrency` group (serialized per ref), so it never interleaves with other runs.

## Code Signing (SignPath)

The pre-release code-signing flow for the Windows artifact (`GitHubDesktop2Chinese.exe`) is built in and currently **dormant** (updated 2026-10).

**Current status**: the SignPath Foundation application was not approved — the program requires a certain level of public visibility signals (community adoption, independent references, external discussions, etc.), which this project does not yet meet; credentials are not configured, the signing steps skip automatically, and **release artifacts are currently unsigned**, with no impact on releases. Artifacts still carry the SLSA build provenance attestation (which does not depend on signing), and the SignPath statement in the Release notes is emitted only when signing actually happens.

- **Enablement**: signing happens only when both the repository Secret `SIGNPATH_API_TOKEN` and Variable `SIGNPATH_ORGANIZATION_ID` are configured; if either is missing it is skipped automatically, the artifact stays unsigned, and the release is unaffected;
- **Overridable settings**: Variables `SIGNPATH_PROJECT_SLUG` (default `GitHubDesktop2Chinese`) and `SIGNPATH_SIGNING_POLICY_SLUG` (default `release-signing`);
- **Flow** (inside the release job): download build artifacts → upload the exe to be signed separately (SignPath requires the signing subject to exist as a GitHub artifact first) → submit the signing request and wait for completion → replace the original exe with the signed one → **regenerate the build provenance attestation** for the signed artifact (signing changes the file content, so the unsigned version's attestation digest no longer matches);
- **Paths to enable it later** (maintainers, either one): **reapply** to [SignPath Foundation](https://signpath.org/apply) once community visibility grows (applications are welcome again), or switch to a [paid subscription](https://docs.signpath.io/change-subscription); once approved, complete the one-time setup below — no code changes needed;
- **One-time SignPath-side setup** (maintainers): create a project (slug matching the repository name; Artifact Configuration referencing [`.signpath/artifact-configuration.xml`](../../.signpath/artifact-configuration.xml); Trusted Build System linked to this repository on GitHub.com) → create the `release-signing` signing policy → generate an API Token and organization ID and store them in repository Secrets / Variables respectively.

## Quality Gate Details (json-quality)

Static validation of `localization.json` on PRs and before releases (script: `tools/ci/check-localization.py`):

1. **Structure and types**: top-level object, required keys, and array element types validated item by item;
2. **Regex validity**: every find regex must compile;
3. **`std::regex` compatibility blacklist**: lookbehind assertions, named capture groups, `\p{…}`, inline flags, and other syntax that is legal in JS but throws at C++ runtime are rejected outright; the blacklist lives in [`tools/auto-maintain/regex-blacklist.json`](../../tools/auto-maintain/regex-blacklist.json) as the single source of truth shared by the CI script and the auto-maintain tool;
4. **ReDoS heuristics**: catastrophic-backtracking patterns are intercepted;
5. **Placeholder checks**: legal usage of `#{n}` and capture groups `$n`;
6. **Translation style**: full-width `？` for questions, trailing spaces in translations;
7. **Entry order**: warnings for mis-ordered "short-in-long" pairs.

## Quality Gate Details (encoding-check)

A full scan of source encodings on PRs and before releases (script: `tools/ci/check-encoding.py`):

1. **Scope**: every git-tracked text file (build artifacts are untracked and naturally excluded);
2. **Rule**: must be UTF-8 (no BOM);
3. **Violations**: GBK / non-UTF-8 content and unexpected BOMs → reported as a per-file `::error` list, and the non-zero exit code fails the workflow;
4. **Exclusions**: third-party dependency directories (their encodings are outside this repository's control); `smoke-test.ps1` is a whitelisted BOM exception (Windows PowerShell 5.1 compatibility).

**Configuration (v2)**: detection parameters are no longer hardcoded in the script — [`tools/ci/encoding-check.json`](../../tools/ci/encoding-check.json) is the single source of truth, passed explicitly by CI via `--config`. Three-level merge precedence: built-in defaults (identical to v1 behavior when no arguments are given) < config file < CLI arguments (`--exclude` / `--allow` append; `--strict` / `--continue-on-violation` override the lenient switch). Config keys:

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `allowed_encodings` | string[] | `["utf-8", "ascii"]` | Allow-list of encodings (lowercase detection labels) |
| `exclude_dirs` | string[] | `["third_party/"]` | Excluded directories (forward-slash prefix match) |
| `bom_exceptions` | string[] | `["tools/ci/smoke-test.ps1"]` | Files allowed to carry a UTF-8 BOM (losing it only warns) |
| `binary_extensions` | string[] | (see the config file) | Binary extensions skipped before reading |
| `sample_size` | int | `8192` | Bytes sampled for BOM / NUL probing |
| `workers` | int | `0` | Parallel threads; `0` = automatic (`min(32, CPU×4)`) |
| `continue_on_violation` | bool | `false` | Lenient mode: violations downgrade to `::warning` with exit code 0 |

**Lenient mode**: for legacy-remediation transitions — violations only emit a `::warning` and never block the pipeline (`--continue-on-violation`, or `continue_on_violation: true` in the config); this repository currently has no legacy violations, so CI stays strict.

**Local usage**:

```bash
python tools/ci/check-encoding.py --config tools/ci/encoding-check.json   # identical to CI
python tools/ci/check-encoding.py --exclude docs/legacy/                  # ad-hoc extra exclusion
```

**FAQ**:

- **Why enforce UTF-8 without BOM?** A BOM in JSON violates RFC 8259; a BOM in `.sh` breaks the shebang; GBK content garbles the Linux CI (see the [#85 audit](https://github.com/Tupig/GitHubDesktop2Chinese/issues/85)).
- **How do I fix a violation?** Convert the file to UTF-8 without BOM (editor or `iconv -f GBK -t UTF-8 <file>`) and re-commit; do not work around it by allow-listing.
- **Why may `smoke-test.ps1` carry a BOM?** Without one, Windows PowerShell 5.1 parses the script as ANSI and garbles the Chinese text; it is the repository's only exception.
- **What if the config file is wrong?** JSON syntax errors / wrong key types → `::error` + exit code 2 (hard failure); unknown keys → `::warning` and ignored.

## What This Means for Contributors

- Quality gates and tool self-tests run automatically at the PR stage; if they pass locally, they generally pass in CI;
- After merging to main, any change to `json/`, `src/`, `third_party/` (excluding the docs-only `third_party/README.md`), `CMakeLists.txt`, or `CMakePresets.json` **needs no manual release** — the next push / daily schedule produces a patch version automatically;
- After `docs/wiki/` changes merge into main, the GitHub Wiki pages sync automatically — no need to maintain two copies by hand;
- To get your change lines into the Release notes, follow the [Contribution Guide](贡献指南.md#4-发布说明docsreleasebodymd) and tag them in `docs/ReleaseBody.md` with the full-width version marker.
