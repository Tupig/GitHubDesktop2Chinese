# auto-maintain — localization.json 自动维护工具

**简体中文** | [English](#auto-maintain--localizationjson-maintenance-tool-english)

自动维护 TuPigGHD2Chinese 的 `json/localization.json`：

1. **失效检测** (`check`)：获取最新 GitHub Desktop（Windows 版），对现有映射逐条检测是否能匹配到 `main.js` / `renderer.js`，找出失效（匹配不到）的映射项。
2. **依序模拟检测** (`simulate`)：按 C++ 加载器的应用顺序（main → renderer → select）依序应用全部映射并逐条统计实际替换次数，找出「单独可匹配、但被前面正则先改写」的隐性失效条目（#207）。
3. **未翻译候选提取** (`extract`)：从最新 GitHub Desktop 中提取尚未被现有映射覆盖的英文 UI 文案候选，并生成可直接粘贴的 JSON 条目草稿。

> 该工具**只产出报告，不直接修改** `localization.json`。所有改动仍需人工确认，避免正则写坏导致 GitHub Desktop 无法打开。

## 运行环境

- Node.js >= 18
- 解压工具：Linux/macOS 需 `unzip`（或 `tar`），Windows 自带 `tar`

## 用法

```bash
cd tools/auto-maintain

# 失效检测（默认读取仓库根目录 json/localization.json，与当前所在目录无关）
node src/index.js check

# 依序模拟检测（可与 check 组合）
node src/index.js simulate

# 模拟检测并输出全量逐条替换计数（默认仅输出隐性失效项）
node src/index.js simulate --verbose

# 未翻译候选提取
node src/index.js extract

# 两者都跑，并输出报告到 workdir/report.md
node src/index.js check extract --write-report

# 指定映射文件路径
node src/index.js check --json /path/to/localization.json

# 本地调试：复用已有的 main.js/renderer.js（跳过约 307MB 下载）
node src/index.js check extract --keep-js

# 运行单元测试（与 CI JsonCheck 同一入口，无需网络）
npm test
```

## 参数

| 参数 | 说明 |
| --- | --- |
| `check` / `simulate` / `extract` / `all` | 子命令，可组合（如 `check simulate extract`；`all` = check + extract） |
| `--json <path>` | localization.json 路径（默认为仓库根目录 `json/localization.json`，按脚本位置推导，不受 cwd 影响） |
| `--workdir <dir>` | 下载/解压工作目录（默认系统临时目录） |
| `--keep-js` | 复用 workdir 中已有的 `main.js`/`renderer.js`，跳过下载 |
| `--top <n>` | extract 输出候选条数（默认 60） |
| `--verbose` | simulate 输出全量逐条替换计数（默认仅输出隐性失效项） |
| `--write-report` | 将完整报告写入 `workdir/report.md` |
| `--no-color` | 关闭彩色输出 |

## 工作原理

### 数据源

从 `desktop/desktop` 的最新 release 下载 `GitHubDesktop-<版本>-x64-full.nupkg`（约 307MB，Windows 版 Squirrel 完整包，本质为 zip）。
包内含 `lib/net45/resources/app/{main.js,renderer.js}`，即打包前（未 asar 压缩）的 Electron 资源，可直接读取。

> 下载完整性三重校验：EOCD 签名、release 资产声明的字节数（尺寸不符删断点重下）、`.part.url` 旁证（断点与本次 URL 不一致时删除，防止跨版本续传拼出混合文件）。

> ⚠️ **不要改用 macOS 的 `GitHub.Desktop-x64.zip`**：两个平台的应用文案不同（Windows 菜单带 `&` 访问键、路径相关文案为 Explorer/Command Prompt 等），
> 用 macOS 包会产生大量假失效（实测 1006 条映射中约 239 条误报）。本项目的汉化目标是 Windows 版。

### 失效检测

与 C++ 主程序的 `--invalidcheck` 逻辑对齐（`TuPigGHD2Chinese.cpp`）：

- 对 `main`/`renderer`/`main_dev`/`renderer_dev` 每个数组的每一项：
  - `item[0]` 作为正则对对应 JS 文本执行 `test`
  - 若 `item[2]`（第三个查找参数）存在，也一并测试
  - 匹配不到记为 `not-found`；正则编译失败记为 `regex-error`；
    使用 C++ `std::regex` 不兼容的语法（后行断言、命名捕获组、`\p{...}`、内联 flag 等，JS 侧可编译但 C++ 运行时会抛错）记为 `unsupported-syntax`
    （规则清单见目录下 `regex-blacklist.json`，与 CI 数据质量校验共用）
  - 疑似灾难性回溯（嵌套量词）记为 `redos-suspect`，**仅告警不阻断**（本地控制台与 report.md 均呈现，与 CI 质量门告警一致）
- 对 `select` 中 `enable=true` 的条目：按 `replaceFile` **精确等于** `main.js` / `renderer.js` 分别用对应 JS 检测（与 C++ 应用侧按字面量相等的判断一致；其它取值整条 select 不生效，直接跳过不计数）

### 依序模拟检测

完整复刻 C++ 加载器的**依序应用**过程（`ProcessJsFile` + `ApplySelectForFile`），逐条统计实际替换次数：

- 模拟范围：`main` + `renderer` + `select`（`enable=true`）全部映射，按数组顺序在**前面条目改写后的文本**上依序应用；不含 `main_dev` / `renderer_dev`（dev 模式专用数组）
- 三元素条目复刻 C++ `FillPlaceholders`：`[2]` 对当前文本取首个匹配、捕获组回填 `#{n}`（`$` 先转义 `$$`）、占位符残留整条跳过
- **隐性失效判定**：某条依序应用时一次都没替换，且 `item[0]`（及三元素条目的 `item[2]`）对**原始未改写文本**能匹配——即条目本身没写错，是被前面正则抢占/改写才失效。按原因输出 `preempted`（被前面正则改写抢占）/ `gate-miss`（条件正则依序失配）/ `gate-leftover`（占位符未回填）
- 误报防护：结构性跳过（空串 / `"\"\""` 哨兵、非法条目）、正则编译失败、原文即不匹配（`not-found` 职责）均不参与隐性判定
- `--verbose` 输出全部条目的逐条替换计数，零替换且原文能匹配时显示独立匹配数，便于人工核对

### 未翻译候选提取

1. 从 `main.js`/`renderer.js` 提取双引号/单引号字符串字面量
2. `isLikelyUiText()` 过滤明显非 UI 文案（内部库消息、URL、路径、错误信息、颜色、正则、模板串、拼接碎片等）
3. 用现有映射的所有正则（含 `select` 的 `replace` 项）做覆盖检测，被任一正则匹配到的视为"已翻译"并排除
4. 剩余候选按出现次数排序输出，并标注来源文件（`main.js` / `renderer.js`）
5. 报告中为每个候选生成 JSON 草稿（查找项按字面量转义），人工将 `【待翻译】` 替换为译文后加入对应数组

> 提取是启发式的，会包含少量噪音（如库内部提示语）。候选用于辅助维护，不代表一定需要翻译（专有名词可跳过）。

## CI 自动维护

工作流位于 `../../.github/workflows/tupigghd2chinese.yml`（仓库根目录）：

- **触发**：push `main` + 每日定时（北京时间 10:00）+ 手动 `workflow_dispatch` 选择 `type=auto` 或 `type=maintain`
- **流程**：查询最新版本 → 恢复缓存（安装包 + 提取结果）→ 跑 `check extract` → 生成报告 → 创建/更新 `auto-maintain` 标签的 Issue
- **自动关 Issue**：当失效项降为 0 时，自动关闭历史维护 Issue
- **不自动合并**：所有映射改动仍需人工确认，避免破坏 GitHub Desktop

> ⚠️ 缓存 key 绑定 GitHub Desktop 版本、平台与工具源码（`ghd-maintain-v3-<版本>-<平台>-<工具哈希>`），安装包或工具变化后自动失效重建。
> 不含 `localization.json` 哈希：映射每次改动都不同，纳入会让 307MB 安装包缓存失效，而报告本就由步骤内重新生成。
> 提取结果与安装包一致时会自动复用（`.source.txt` 来源标记），跳过重复解压。

## 目录结构

```
tools/auto-maintain/
├── package.json
├── regex-blacklist.json   # std::regex 不兼容语法黑名单（CI 数据质量校验共用）
├── src/
│   ├── index.js           # CLI 入口
│   ├── args.js            # CLI 参数解析（无副作用纯模块，单测覆盖各分支）
│   ├── fetch.js           # 获取/下载/解压最新 GitHub Desktop，提取 main.js/renderer.js
│   ├── check-invalid.js   # 失效检测
│   ├── simulate.js        # 依序模拟检测（按 C++ 应用顺序逐条替换计数，发现隐性抢占）
│   ├── extract-new.js     # 未翻译候选提取
│   └── report.js          # Markdown 报告渲染（含表格行数上限防护）
```

---

# auto-maintain — localization.json Maintenance Tool (English)

[简体中文](#auto-maintain--localizationjson-自动维护工具) | **English**

Automates maintenance of TuPigGHD2Chinese's `json/localization.json`:

1. **Invalid-entry check** (`check`): fetches the latest GitHub Desktop (Windows edition) and tests every existing mapping against `main.js` / `renderer.js` to find stale (no-longer-matching) entries.
2. **Sequential simulation detection** (`simulate`): applies all mappings in the C++ loader's order (main → renderer → select), counting actual replacements per entry, to find entries that silently stop working **only when applied in order** because an earlier regex rewrites their target first (#207).
3. **Untranslated candidate extraction** (`extract`): extracts English UI string candidates from the latest GitHub Desktop that are not yet covered by existing mappings, and produces ready-to-paste JSON entry drafts.

> The tool **only produces reports; it never modifies** `localization.json` directly. All changes still require manual confirmation, to avoid a broken regex leaving GitHub Desktop unable to start.

## Requirements

- Node.js >= 18
- An unzip tool: Linux/macOS need `unzip` (or `tar`); Windows ships with `tar`

## Usage

```bash
cd tools/auto-maintain

# Invalid-entry check (reads json/localization.json at the repository root by default, regardless of cwd)
node src/index.js check

# Sequential simulation detection (combinable with check)
node src/index.js simulate

# Simulate and print per-entry replacement counts (by default only hidden failures are printed)
node src/index.js simulate --verbose

# Untranslated candidate extraction
node src/index.js extract

# Run both and write the report to workdir/report.md
node src/index.js check extract --write-report

# Specify a mapping file path
node src/index.js check --json /path/to/localization.json

# Local debugging: reuse existing main.js/renderer.js (skips the ~307MB download)
node src/index.js check extract --keep-js

# Run the unit tests (same entry point as the CI JsonCheck job; no network needed)
npm test
```

## Options

| Option | Description |
| --- | --- |
| `check` / `simulate` / `extract` / `all` | Subcommands, combinable (e.g. `check simulate extract`; `all` = check + extract) |
| `--json <path>` | Path to localization.json (defaults to `json/localization.json` at the repository root, resolved from the script location and unaffected by cwd) |
| `--workdir <dir>` | Download/extraction working directory (defaults to the system temp directory) |
| `--keep-js` | Reuse the `main.js`/`renderer.js` already in workdir, skipping the download |
| `--top <n>` | Number of candidates extract outputs (default 60) |
| `--verbose` | Have simulate print per-entry replacement counts (by default only hidden failures are printed) |
| `--write-report` | Write the full report to `workdir/report.md` |
| `--no-color` | Disable colored output |

## How It Works

### Data Source

Downloads `GitHubDesktop-<version>-x64-full.nupkg` (~307MB, the Windows Squirrel full package — essentially a zip) from the latest `desktop/desktop` release.
It contains `lib/net45/resources/app/{main.js,renderer.js}` — the Electron resources before packaging (not asar-packed) — which can be read directly.

> Download integrity is triple-checked: the EOCD signature, the byte size declared by the release asset (a mismatch deletes the partial file and restarts), and the `.part.url` sidecar (deleted when the resume URL differs from the current one, preventing cross-version resumed downloads from producing a hybrid file).

> ⚠️ **Do not switch to the macOS `GitHub.Desktop-x64.zip`**: application strings differ between the two platforms (Windows menus carry `&` access keys; path-related strings reference Explorer/Command Prompt, etc.).
> The macOS package produces massive false positives (in practice, about 239 false stale entries out of 1006 mappings). This project's localization target is the Windows edition.

### Invalid-Entry Check

Aligned with the C++ main program's `--invalidcheck` logic (`TuPigGHD2Chinese.cpp`):

- For every item of each `main`/`renderer`/`main_dev`/`renderer_dev` array:
  - `item[0]` is treated as a regex and `test`ed against the corresponding JS text
  - If `item[2]` (the third search parameter) exists, it is tested too
  - No match is recorded as `not-found`; a regex compile failure as `regex-error`;
    syntax incompatible with C++ `std::regex` (lookbehind assertions, named capture groups, `\p{...}`, inline flags, etc. — compilable on the JS side but throwing at C++ runtime) is recorded as `unsupported-syntax`
    (the rule list lives in `regex-blacklist.json` in this directory, shared with the CI data quality check)
  - Suspected catastrophic backtracking (nested quantifiers) is recorded as `redos-suspect` — **a warning only, never blocking** (shown in both the local console and report.md, consistent with the CI quality-gate warning)
- For `select` entries with `enable=true`: tested against the corresponding JS per `replaceFile` **exactly equal to** `main.js` / `renderer.js` (consistent with the C++ app-side literal-equality check; any other value makes the whole select entry ineffective — skipped without counting)

### Sequential Simulation Detection

Replays the C++ loader's full **in-order application** (`ProcessJsFile` + `ApplySelectForFile`), counting the actual replacements of every entry:

- Scope: all `main` + `renderer` + `select` (`enable=true`) mappings, applied in array order against the text **as rewritten by previous entries**; `main_dev` / `renderer_dev` are excluded (dev-mode-only arrays)
- Three-element entries replicate the C++ `FillPlaceholders`: `[2]` takes its first match against the current text, capture groups backfill `#{n}` (`$` escaped to `$$` first), and any leftover placeholder skips the whole entry
- **Hidden-failure criteria**: an entry produces zero replacements when applied in order, while `item[0]` (and `item[2]` for three-element entries) still matches the **original, unrewritten text** — i.e. the entry itself is not wrong; it was preempted/rewritten away by an earlier regex. Reported by cause: `preempted` (target rewritten by an earlier regex) / `gate-miss` (gate regex failing in order) / `gate-leftover` (placeholders left unbackfilled)
- False-positive protection: structural skips (empty strings / `"\"\""` sentinels, invalid entries), regex compile failures, and entries that fail to match even the original text (`not-found`'s job) never count as hidden
- `--verbose` prints per-entry replacement counts for every entry, showing the standalone match count for zero-replacement entries that still match the original text, for manual verification

### Untranslated Candidate Extraction

1. Extract double-quoted/single-quoted string literals from `main.js`/`renderer.js`
2. `isLikelyUiText()` filters out clearly non-UI strings (internal library messages, URLs, paths, error messages, colors, regexes, template strings, concatenation fragments, etc.)
3. Coverage check against all regexes of existing mappings (including `select` `replace` items); anything matched by any regex is considered "already translated" and excluded
4. Remaining candidates are sorted by occurrence count and annotated with their source file (`main.js` / `renderer.js`)
5. The report generates a JSON draft for each candidate (search terms escaped as literals); a human replaces `【待翻译】` with the translation and adds it to the corresponding array

> Extraction is heuristic and includes some noise (e.g. internal library hints). Candidates assist maintenance and do not necessarily need translating (proper nouns can be skipped).

## CI Automated Maintenance

The workflow lives at `../../.github/workflows/tupigghd2chinese.yml` (repository root):

- **Triggers**: push to `main` + daily schedule (10:00 Beijing time) + manual `workflow_dispatch` with `type=auto` or `type=maintain`
- **Flow**: query the latest version → restore caches (installer + extraction results) → run `check extract` → generate the report → create/update the `auto-maintain` labeled issue
- **Auto-closing issues**: historical maintenance issues are closed automatically once stale entries drop to 0
- **Never auto-merges**: all mapping changes still require manual confirmation, to avoid breaking GitHub Desktop

> ⚠️ The cache key binds the GitHub Desktop version, platform, and tool source (`ghd-maintain-v3-<version>-<platform>-<tool hash>`); it is invalidated and rebuilt automatically when the installer or tool changes.
> It deliberately excludes the `localization.json` hash: mappings change every time, and including it would invalidate the 307MB installer cache, while the report is regenerated by the step anyway.
> Extraction results matching the installer are reused automatically (`.source.txt` source marker), skipping redundant extraction.

## Directory Layout

```
tools/auto-maintain/
├── package.json
├── regex-blacklist.json   # Denylist of std::regex-incompatible syntax (shared with the CI data quality check)
├── src/
│   ├── index.js           # CLI entry
│   ├── args.js            # CLI argument parsing (side-effect-free pure module, unit-tested)
│   ├── fetch.js           # Fetch/download/extract the latest GitHub Desktop, extract main.js/renderer.js
│   ├── check-invalid.js   # Invalid-entry check
│   ├── simulate.js        # Sequential simulation detection (per-entry replacement counts in C++ application order, finds hidden preemption)
│   ├── extract-new.js     # Untranslated candidate extraction
│   └── report.js          # Markdown report rendering (with table row-limit protection)
```
