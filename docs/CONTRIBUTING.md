# 汉化贡献指南

**简体中文** | [English](#localization-contribution-guide-english)

感谢参与 GitHubDesktop2Chinese 的汉化贡献！补充或修改翻译（`json/localization.json`）前，请先阅读以下注意事项。

> 程序使用**正则表达式**进行匹配替换，理解以下规则可以避免「匹配不到」或「替换后程序打不开」的问题。
>
> 本页为速览版规则。完整的替换原理、映射字段说明与正则教程见 [wiki 贡献指南](wiki/贡献指南.md)、[wiki 映射文件格式](wiki/映射文件格式.md)与[wiki 正则表达式指南](wiki/正则表达式指南.md)。

## 核心规则

1. **匹配的双引号必须使用反斜杠转义**，不可使用「最外层单引号、匹配内容双引号不转义」的写法：

   - 错误：`['i am "zhangsan"','我是"张三"']`
   - 正确：`["i am \"zhangsan\"","我是\"张三\""]`

2. **支持正则匹配**：程序使用正则表达式进行匹配修改，查找项天然支持正则语法。

3. **`?` 的转义**：若有匹配 `?`（该字符）的情况，请在 `?` 前使用两个反斜杠；匹配后填充的内容（译文）中 `?` 前不要使用反斜杠。

4. **匹配字符串建议带上引号**：如 `["\"&View\"","\"视图\""]`，以避免误匹配到 `&View on GitHub` 等更长文本；如果误替换了这些长文本，后面的映射就匹配不到了。

5. **统一转义规则**：无论是 `?` 还是其他正则特殊符号，都需要使用两个反斜杠转义；只有双引号是只需要一个反斜杠转义。所有为正则表达式而添加的反斜杠，在译文部分都不需要也不能有。

   常用正则符号速查：

   | 符号 | 含义 |
   | --- | --- |
   | `.` | 匹配任何单个字符（除了换行符） |
   | `*` | 匹配前面的子表达式零次或多次 |
   | `+` | 匹配前面的子表达式一次或多次 |
   | `?` | 匹配前面的子表达式零次或一次 |
   | `{n}` / `{n,}` / `{n,m}` | 匹配确定的 n 次 / 至少 n 次 / n 到 m 次 |
   | `^` / `$` | 匹配输入字符串的开始 / 结束位置 |
   | `[...]` | 字符集，匹配方括号中的任何字符 |
   | `|` | 或者，匹配 `|` 前后的任何表达式 |
   | `( )` | 捕获括号 |
   | `\` | 转义字符：匹配特殊字符字面值或反斜杠本身时，需在其前面加上 `\` |

6. **JSON 语法**：数组最后一项结尾不可有逗号，否则无法读取。如 `[["xx","xxx"],["",""],]` 是错误写法。

7. **善用正则模糊匹配**：如 `Ee.createElement\\(yD,null,this.props.repositoryName` 中的函数参数 `yD` 常因版本更新而变动，可利用正则模糊匹配这两位字符，使其适应性更强：

   - 查找：`Ee.createElement\\((..),null,this.props.repositoryName`（查找项是正则，JSON 中需 `\\(` 转义出正则的 `\(`）
   - 替换：`Ee.createElement($1,null,this.props.repositoryName`（替换文本按**字面量**插入，不可带反斜杠——写成 `\\(` 会让生成的 main.js 出现 `createElement\(` 而语法报错；`$1` 表示第一个正则捕获到的内容）

8. **使用自动维护工具辅助补充翻译**（会生成可直接粘贴的条目草稿）：

   ```bash
   cd tools/auto-maintain
   npm run all
   ```

   报告位于系统临时目录 `ghdesktop-auto-maintain/report.md`，按「来源」列把条目放入 `main`（main.js）或 `renderer`（renderer.js）数组，将草稿中的 `【待翻译】` 替换为中文译文即可（查找项已按字面量转义）。

   > [!NOTE]
   > **条目顺序**：查找正则逐字面从头替换，先匹配者生效。新条目若与已有条目「短含长」（查找项是另一条的子串，如 `"Save"` 与 `"Save":"Add"`），须**插在更宽泛（更短）条目之前**，否则宽泛条目会先命中、专条失效。CI 的质量校验会对此告警。
   >
   > **译文风格**：句末问号统一使用全角 `？`；译文末尾不留多余空格。

## 本地验证改动

1. 按 [README「怎么编译源代码」](../README.md#-怎么编译源代码) 编译加载器。
2. **快速验证**：把新条目放入 `main_dev` / `renderer_dev`，启动时按住 `Shift` 进入开发者选项，选择「仅替换指定映射项」；验证通过后再移动到正式数组。
3. **批量自检失效条目**（推荐提交前运行；需本机已安装 GitHub Desktop，或用 `-g` 指定 js 目录）：

   ```powershell
   GitHubDesktop2Chinese.exe --nopause dev --invalidcheck --translationfrombak
   ```

   > `--nopause` 为程序级参数，必须写在 `dev` 子命令之前；输出会逐个报告匹配失败 / 正则非法的条目。
4. 也可用自动维护工具离线检测与提取候选：`cd tools/auto-maintain && npm run all`，详见[工具说明](../tools/auto-maintain/README.md)（参数与报告格式另见 [wiki 自动维护工具](wiki/自动维护工具.md)）。
5. 加载器（`src/`）改动的冒烟回归：运行 [`tools/ci/smoke-test.ps1`](../tools/ci/smoke-test.ps1)，自动探测构建产物并依次执行帮助、非法参数、临时目录汉化、回滚四组用例，全部 PASS 即通过。

## 发布说明（docs/ReleaseBody.md）

`json/`、`src/`、`third_party/`、`CMakeLists.txt`、`CMakePresets.json` 任一变更都会在 push main / 每日定时 / 手动 `auto` 时自动触发新版本发布（版本号按进位规则自动递增）。CI 发布时从 `docs/ReleaseBody.md` 中提取**行尾含全角版本标记**（如 `（v0.0.2）`）的变更行拼进 Release 说明：

- 在「版本更新日志」区域追加一行中文变更描述与一行对应英文翻译，两行行尾均标注**下一个发布版本**的全角括号标记（用 `git tag --sort=-v:refname | head -1` 查当前最新 tag，按进位规则递增：patch +1，满 100 进位 minor 并重置为 1）。
- 未标注或版本号未命中的行不会出现在任何 Release 中，也**不阻断**发布（说明里会显示「未找到变更记录」提示）。
- 静态的「### 程序说明」段落每次发布都会附带，无需逐版维护。

## 开发工作流

- **任务先行**：每项任务开工前先建 issue，提交信息与 PR 均关联该 issue 编号。
- **分支模型**：`main` 受分支保护（禁强推、禁删除、管理员同样受限、要求 1 个 approval、对话全部解决、线性历史）；功能与修复一律走 `<type>/<短描述>` feature 分支 + PR，如 `ci/cron-beijing-0923`。
- **提交信息**：遵循 [Conventional Commits](https://www.conventionalcommits.org/zh-hans/)——`<type>(<scope>): <subject>(#<issue>)`，`type` 取 `feat` / `fix` / `docs` / `ci` / `refactor` / `test` / `chore`，`scope` 可选。仓库根目录提供 `.gitmessage` 模板，执行 `git config commit.template .gitmessage` 后 `git commit` 自动载入。
- **PR 必需检查**：以下三个 status checks 为合并必需（分支保护配置）：

  - `localization.json 质量校验`
  - `源码编码格式检查`
  - `自动维护工具自检`

  其余 checks（4 平台构建矩阵、CodeQL、版本解析等）须通过但不设为必需；PR 获 1 个 approve 且对话全部解决后 squash merge。

## 合并后分支清理

仓库已开启 `delete_branch_on_merge`，PR 合并后远端分支自动删除；本地分支按以下流程清理：

```powershell
git switch main
git pull --ff-only
git fetch --prune
gh pr view <PR 编号> --json state --jq .state   # 必须输出 MERGED
git branch -D <分支名>
```

> 本仓库使用 squash merge：squash 合并过的分支不会出现在 `git branch --merged` 中，`git branch -d` 也会拒绝删除；**必须以 `gh pr view` 确认 `MERGED` 后再用 `-D` 删除**。`main` 永远不删除。

## 依赖策略

构建遵循**自包含**原则（审计与实施计划见 [#98](https://github.com/Tupig/GitHubDesktop2Chinese/issues/98)）：

- Windows / macOS / Linux 三平台并行编译打包，产物一致、可直接发布。
- 所有依赖**内置仓库，或仅使用 GitHub 原生功能**（Actions、Releases、Packages、cache）；不引入外部第三方依赖服务。
- OpenSSL 三平台统一 **3.5.9 静态链接**：Windows / macOS / Linux 均使用内置 `third_party/` 成品库，克隆即可离线构建；成品库由主工作流的产库（vendor）任务在 GitHub Actions 上构建并提交，供未来升级复用。
- C++ 头文件依赖（CLI11、cpp-httplib、nlohmann/json、spdlog、WinReg）内置 `third_party/include/`。
- GitHub Actions 全部使用官方 action 并以 commit SHA 固定。
- SignPath 代码签名为唯一例外：由 secrets 门控，未配置时自动跳过，不产生外部调用。
- **自包含断言**：macOS 产物 `otool -L` 不得出现 Homebrew OpenSSL 路径，`lipo` 确认双架构；Linux 产物 `ldd` 不得出现 `libssl` / `libcrypto`。

## 标签约定

| 标签 | 用途 |
| --- | --- |
| `ci-cd` | CI/CD 工作流与自动化 |
| `localization` | 翻译与本地化内容 |
| `refactor` | 代码重构与结构优化 |
| `performance` | 性能优化 |
| `security` | 安全问题与漏洞 |
| `auto-maintain` | 自动维护工具 |

另有 GitHub 默认标签（`bug`、`enhancement`、`documentation`、`good first issue` 等）按语义使用。

## 行为准则

参与贡献即表示你同意遵守[行为准则](../.github/CODE_OF_CONDUCT.md)。交流请保持友善；安全漏洞请勿在 Issue 或 PR 中公开讨论，走[私密漏洞报告](../SECURITY.md)。

---

# Localization Contribution Guide (English)

[简体中文](#汉化贡献指南) | **English**

Thanks for contributing translations to GitHubDesktop2Chinese! Before adding or modifying translations (`json/localization.json`), please read the notes below.

> The program matches and replaces text with **regular expressions**. Understanding the rules below helps you avoid "pattern does not match" failures or "GitHub Desktop won't open after replacement" breakage.
>
> This page is the quick-reference edition. For the full replacement internals, the mapping-field reference and the regex tutorial, see the [wiki contribution guide](wiki/贡献指南.md), the [wiki mapping file format](wiki/映射文件格式.md) and the [wiki regular expression guide](wiki/正则表达式指南.md).

## Core Rules

1. **Double quotes inside matched text must be escaped with a backslash**; the style of "outer single quotes with unescaped inner double quotes" is not allowed:

   - Incorrect: `['i am "zhangsan"','我是"张三"']`
   - Correct: `["i am \"zhangsan\"","我是\"张三\""]`

2. **Regex matching is supported**: the program matches and rewrites text with regular expressions, so find patterns natively support regex syntax.

3. **Escaping `?`**: to match a literal `?` character, use two backslashes before it in the find pattern; in the filled-in content (the translation), do not put a backslash before `?`.

4. **Include quote boundaries in find patterns**: e.g. `["\"&View\"","\"视图\""]`, to avoid accidentally matching longer texts such as `&View on GitHub`; if those longer texts get rewritten, later mappings can no longer match them.

5. **Unified escaping rules**: whether it is `?` or any other regex special character, escape it with two backslashes; only the double quote takes a single backslash. Backslashes added for regex purposes must never appear in the translation part.

   Quick reference for common regex symbols:

   | Symbol | Meaning |
   | --- | --- |
   | `.` | Matches any single character (except a newline) |
   | `*` | Matches the preceding sub-expression zero or more times |
   | `+` | Matches the preceding sub-expression one or more times |
   | `?` | Matches the preceding sub-expression zero or one time |
   | `{n}` / `{n,}` / `{n,m}` | Matches exactly n times / at least n times / n to m times |
   | `^` / `$` | Matches the start / end position of the input string |
   | `[...]` | Character set: matches any character in the brackets |
   | `|` | Alternation: matches any expression before or after `|` |
   | `( )` | Capturing group |
   | `\` | Escape character: to match a literal special character or a backslash itself, precede it with `\` |

6. **JSON syntax**: the last item of an array must not be followed by a comma, otherwise the file cannot be read. For example, `[["xx","xxx"],["",""],]` is incorrect.

7. **Use regex fuzzy matching wisely**: function arguments such as `yD` in `Ee.createElement\\(yD,null,this.props.repositoryName` often change between GitHub Desktop releases; match those two characters fuzzily with a regex to make the entry more resilient:

   - Find: `Ee.createElement\\((..),null,this.props.repositoryName` (the find pattern is a regex; in JSON, `\\(` escapes to the regex `\(`)
   - Replace: `Ee.createElement($1,null,this.props.repositoryName` (the replacement text is inserted **literally** and must not contain backslashes — writing `\\(` would emit `createElement\(` into the generated main.js and cause a syntax error; `$1` refers to the content captured by the first regex group)

8. **Use the auto-maintain tool to help add translations** (it generates ready-to-paste entry drafts):

   ```bash
   cd tools/auto-maintain
   npm run all
   ```

   The report is written to `ghdesktop-auto-maintain/report.md` in the system temp directory. Following its "source" column, put the entries into the `main` (main.js) or `renderer` (renderer.js) array, and replace the `【待翻译】` placeholder in each draft with the Chinese translation (find patterns are already escaped as literals).

   > [!NOTE]
   > **Entry order**: find patterns are applied from the top of the array and the first match wins. If a new entry is "short-in-long" with an existing one (its find pattern is a substring of another's, e.g. `"Save"` vs `"Save":"Add"`), it must be **inserted before the broader (shorter) entry** — otherwise the broader entry matches first and the specific one never applies. CI quality checks warn about this.
   >
   > **Translation style**: sentence-final question marks use the full-width `？`; do not leave trailing spaces in translations.

## Verifying Changes Locally

1. Build the loader following the [README "Building from source"](../README.md#-怎么编译源代码) section.
2. **Quick check**: put the new entries into `main_dev` / `renderer_dev`, hold `Shift` at startup to open the developer options and pick "replace only specified mappings"; move them into the official arrays after verifying.
3. **Batch self-check for invalid entries** (recommended before submitting; requires GitHub Desktop installed locally, or pass its js directory via `-g`):

   ```powershell
   GitHubDesktop2Chinese.exe --nopause dev --invalidcheck --translationfrombak
   ```

   > `--nopause` is a program-level flag and must come **before** the `dev` subcommand; the output reports each entry whose pattern fails to match or whose regex is invalid.
4. You can also use the auto-maintain tool for offline checking and candidate extraction: `cd tools/auto-maintain && npm run all` — see the [tool README](../tools/auto-maintain/README.md) (parameters and report format are also covered in the [wiki auto-maintain tool](wiki/自动维护工具.md) page).
5. Smoke regression for loader (`src/`) changes: run [`tools/ci/smoke-test.ps1`](../tools/ci/smoke-test.ps1); it auto-detects the build output and runs four test groups in turn — help, invalid-argument rejection, localization in a temp directory, and rollback. All groups PASS means the change passes.

## Release Notes (docs/ReleaseBody.md)

Any change to `json/`, `src/`, `third_party/`, `CMakeLists.txt` or `CMakePresets.json` automatically publishes a new version (the version auto-increments per the carry rule) on push to main, on the daily schedule, or on manual `auto`. When publishing, CI extracts the change lines from `docs/ReleaseBody.md` whose **line ends with the full-width version marker** (e.g. `（v0.0.2）`) and assembles them into the release notes:

- Append a Chinese change line and its English companion line to the "版本更新日志 / Changelog" section, both ending with the full-width version marker of the **next release version** (check the current latest tag with `git tag --sort=-v:refname | head -1`, then increment per the carry rule: patch +1, carrying into minor at 100 and resetting to 1).
- Lines without a marker, or whose version number does not match the release, never appear in any release, and never block publishing (the notes will show a "no change record found" notice instead).
- The static "### 程序说明" section is attached to every release as-is; no per-version maintenance is needed.

## Development Workflow

- **Issue first**: open an issue before starting any task, and reference its number in commits and PRs.
- **Branching**: `main` is protected (no force push, no deletion, admins included, 1 approval required, all conversations resolved, linear history); features and fixes always go through `<type>/<short-description>` feature branches + PRs, e.g. `ci/cron-beijing-0923`.
- **Commit messages**: follow [Conventional Commits](https://www.conventionalcommits.org/) — `<type>(<scope>): <subject>(#<issue>)`, where `type` is one of `feat` / `fix` / `docs` / `ci` / `refactor` / `test` / `chore` and `scope` is optional. A `.gitmessage` template is provided at the repo root; run `git config commit.template .gitmessage` so `git commit` loads it automatically.
- **Required PR checks**: the following three status checks are required for merging (branch protection):

  - `localization.json 质量校验`
  - `源码编码格式检查`
  - `自动维护工具自检`

  All other checks (the 4-platform build matrix, CodeQL, version resolution, ...) must pass but are not required; after 1 approval and all conversations resolved, PRs are squash-merged.

## Post-Merge Branch Cleanup

`delete_branch_on_merge` is enabled, so the remote branch is deleted automatically once a PR merges. Clean up local branches as follows:

```powershell
git switch main
git pull --ff-only
git fetch --prune
gh pr view <PR-number> --json state --jq .state   # must print MERGED
git branch -D <branch-name>
```

> This repo uses squash merge: squash-merged branches never appear in `git branch --merged`, and `git branch -d` refuses to delete them; **confirm `MERGED` via `gh pr view` before deleting with `-D`**. Never delete `main`.

## Dependency Policy

The build is **self-contained** (audit & implementation plan: [#98](https://github.com/Tupig/GitHubDesktop2Chinese/issues/98)):

- Windows / macOS / Linux are built in parallel with consistent, releasable artifacts.
- All dependencies are **vendored in-repo or use GitHub-native features only** (Actions, Releases, Packages, cache); no external third-party services.
- OpenSSL is statically linked at **3.5.9** on all platforms: Windows / macOS / Linux all use the vendored libraries under `third_party/` — clone and build offline; the libraries are built and committed by the main workflow's vendor jobs on GitHub Actions, reusable for future upgrades.
- C++ header dependencies (CLI11, cpp-httplib, nlohmann/json, spdlog, WinReg) are vendored in `third_party/include/`.
- GitHub Actions uses official actions pinned by commit SHA.
- SignPath code signing is the only exception: gated by repository secrets and skipped automatically when unconfigured, producing no external calls.
- **Self-containment assertions**: `otool -L` on macOS artifacts must not show Homebrew OpenSSL paths and `lipo` must confirm both architectures; `ldd` on Linux artifacts must not show `libssl` / `libcrypto`.

## Label Conventions

| Label | Purpose |
| --- | --- |
| `ci-cd` | CI/CD workflows & automation |
| `localization` | Translation & localization content |
| `refactor` | Refactoring & structural improvements |
| `performance` | Performance optimization |
| `security` | Security issues & vulnerabilities |
| `auto-maintain` | Auto-maintain tooling |

GitHub's default labels (`bug`, `enhancement`, `documentation`, `good first issue`, ...) are used by semantics.

## Code of Conduct

By contributing, you agree to follow the [Code of Conduct](../.github/CODE_OF_CONDUCT.md). Please keep discussions friendly; do not discuss security vulnerabilities in public Issues or PRs — use [private vulnerability reporting](../SECURITY.md) instead.
