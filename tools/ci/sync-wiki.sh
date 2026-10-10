#!/usr/bin/env bash
# =====================================================================
# sync-wiki.sh — 同步 docs/wiki/ 到 GitHub Wiki 仓库（镜像语义）
#
# 用法（CI 内由 .github/workflows/wiki-sync.yml 调用；本地亦可手动运行）:
#   GH_TOKEN=<token> bash tools/ci/sync-wiki.sh
#
# 语义:
#   - docs/wiki/*.md 为唯一真值来源，Wiki 线上页面是其镜像
#   - 基于远端最新 HEAD 提交（非 force push），保留 Wiki 编辑历史
#   - 源中不存在的页面会被删除；Wiki web UI 上的手动编辑将被覆盖
#     （更新文档请修改 docs/wiki/ 后合入 main，勿直接编辑 Wiki 页面）
# =====================================================================
set -euo pipefail

REPO_FULL="${GITHUB_REPOSITORY:-Tupig/GitHubDesktop2Chinese}"
: "${GH_TOKEN:?缺少 GH_TOKEN 环境变量（CI 中为 GITHUB_TOKEN，本地运行需手动提供）}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="${SCRIPT_DIR}/../../docs/wiki"

if [ ! -d "$SRC_DIR" ]; then
  echo "错误: 未找到文档源目录 $SRC_DIR" >&2
  exit 1
fi

MD_COUNT="$(find "$SRC_DIR" -maxdepth 1 -name '*.md' | wc -l)"
echo "文档源: $SRC_DIR（$MD_COUNT 个 .md 文件）"

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

echo "克隆 Wiki 仓库 ${REPO_FULL}.wiki.git ..."
git clone --quiet "https://x-access-token:${GH_TOKEN}@github.com/${REPO_FULL}.wiki.git" "$WORK_DIR/wiki"
cd "$WORK_DIR/wiki"

# 镜像语义: 清空 Wiki 工作区（保留 .git）后用文档源覆盖
find . -mindepth 1 -maxdepth 1 ! -name '.git' -exec rm -rf {} +
cp "$SRC_DIR"/*.md .

git add -A
if git diff --cached --quiet; then
  echo "Wiki 与 docs/wiki 完全一致，无需同步"
  exit 0
fi

git config user.name "github-actions[bot]"
git config user.email "41898282+github-actions[bot]@users.noreply.github.com"

SRC_SHA="${GITHUB_SHA:-本地手动运行}"
git commit --quiet -m "docs: 同步 docs/wiki → GitHub Wiki（源提交 ${SRC_SHA:0:7}）"

# 推送; 远端瞬时前进（Wiki 被并发编辑）时 rebase 一次重试,
# 仍失败则报错退出——工作流下次触发会基于最新 HEAD 自动恢复
if ! git push --quiet; then
  echo "推送被拒（远端有新提交），rebase 后重试 ..."
  git pull --rebase --quiet
  git push --quiet
fi

echo "同步完成:"
git log --oneline -1
git diff --stat HEAD~1 HEAD || true
