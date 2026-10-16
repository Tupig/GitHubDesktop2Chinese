#!/usr/bin/env bash
# 功能级测试：tools/ci 下需要真实副作用边界的 Bash 脚本(#283)。
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

fail() {
  echo "::error::$*" >&2
  exit 1
}

assert_fails() {
  if "$@"; then
    fail "命令本应失败却成功: $*"
  fi
}

COMMON="$ROOT_DIR/tools/ci/vendor-openssl-common.sh"
VERIFY="$ROOT_DIR/tools/ci/verify-openssl-tarball.sh"
SYNC="$ROOT_DIR/tools/ci/sync-wiki.sh"

printf 'fixture\n' > "$TMP_DIR/fixture"
EXPECTED_HASH="$(sha256sum "$TMP_DIR/fixture" | cut -d' ' -f1)"
[[ "$(bash "$COMMON" hash "$TMP_DIR/fixture")" == "$EXPECTED_HASH" ]] \
  || fail "共用脚本 hash 正例失败"
assert_fails bash "$COMMON" hash "$TMP_DIR/missing"

PREFIX="$TMP_DIR/prefix"
mkdir -p "$PREFIX/include/openssl" "$PREFIX/lib"
touch "$PREFIX/include/openssl/ssl.h" "$PREFIX/lib/libssl.a" "$PREFIX/lib/libcrypto.a"
printf '%s\n' "$EXPECTED_HASH" > "$PREFIX/tarball.sha256"
[[ "$(bash "$COMMON" verify "$PREFIX" unix)" == "$EXPECTED_HASH" ]] \
  || fail "共用脚本 verify 正例失败"
rm "$PREFIX/lib/libcrypto.a"
assert_fails bash "$COMMON" verify "$PREFIX" unix

# verify-openssl-tarball 正例：用隔离的 sha256sum 桩固定已登记版本哈希，
# 同时仍执行版本/文件存在性与映射表比对逻辑；反例验证格式守卫。
STUB_BIN="$TMP_DIR/stub-bin"
mkdir -p "$STUB_BIN"
REAL_SHA256SUM="$(command -v sha256sum)"
export STUB_TARGET="$TMP_DIR/fixture" \
  STUB_HASH="603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a" \
  REAL_SHA256SUM
cat > "$STUB_BIN/sha256sum" <<'EOF'
#!/usr/bin/env bash
if [[ "${1:-}" == "$STUB_TARGET" ]]; then
  printf '%s  %s\n' "$STUB_HASH" "$1"
else
  exec "$REAL_SHA256SUM" "$@"
fi
EOF
chmod +x "$STUB_BIN/sha256sum"
PATH="$STUB_BIN:$PATH" bash "$VERIFY" 3.5.9 "$TMP_DIR/fixture"
assert_fails bash "$VERIFY" 3.5 "$TMP_DIR/fixture"

# sync-wiki 正例：使用本地 bare git 仓库验证镜像覆盖与推送；反例验证空源目录保护。
SOURCE_DIR="$TMP_DIR/wiki-source"
REMOTE_DIR="$TMP_DIR/wiki.git"
SEED_DIR="$TMP_DIR/wiki-seed"
mkdir -p "$SOURCE_DIR" "$SEED_DIR"
printf '# Home\n' > "$SOURCE_DIR/Home.md"
printf 'old\n' > "$SEED_DIR/Old.md"
git init --bare --quiet "$REMOTE_DIR"
git -C "$SEED_DIR" init --quiet
git -C "$SEED_DIR" config user.name test
git -C "$SEED_DIR" config user.email test@example.invalid
git -C "$SEED_DIR" add Old.md
git -C "$SEED_DIR" commit --quiet -m seed
git -C "$SEED_DIR" branch -M main
git -C "$SEED_DIR" remote add origin "$REMOTE_DIR"
git -C "$SEED_DIR" push --quiet --set-upstream origin main
git --git-dir "$REMOTE_DIR" symbolic-ref HEAD refs/heads/main
GH_TOKEN=test WIKI_SOURCE_DIR="$SOURCE_DIR" WIKI_REMOTE_URL="$REMOTE_DIR" bash "$SYNC"
CHECKOUT_DIR="$TMP_DIR/wiki-check"
git clone --quiet "$REMOTE_DIR" "$CHECKOUT_DIR"
[[ -f "$CHECKOUT_DIR/Home.md" && ! -f "$CHECKOUT_DIR/Old.md" ]] \
  || fail "sync-wiki 正例未镜像源目录"
EMPTY_DIR="$TMP_DIR/empty-source"
mkdir -p "$EMPTY_DIR"
assert_fails env GH_TOKEN=test WIKI_SOURCE_DIR="$EMPTY_DIR" WIKI_REMOTE_URL="$REMOTE_DIR" bash "$SYNC"

echo "Bash 功能级测试通过"
