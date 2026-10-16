#!/usr/bin/env bash
# =====================================================================
# vendor-openssl-common.sh — OpenSSL 产库任务共用逻辑(#283)
#
# 子命令:
#   resolve [版本]                 输出指定或 3.5 LTS 最新版本
#   fetch <版本> <runner-temp>     下载、校验并解压源码到 runner-temp/openssl-src
#   hash <文件>                    输出规范化 SHA256
#   verify <前缀> <windows|unix> [源码包哈希]
#                                  校验头文件、库文件与缓存中的源码包哈希
# =====================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

die() {
  echo "::error::$*" >&2
  exit 1
}

resolve_version() {
  local input="${1:-}"
  local version=""
  if [[ -n "$input" ]]; then
    version="$input"
  else
    for attempt in 1 2 3; do
      version="$(gh api "repos/openssl/openssl/releases?per_page=100" \
        --jq '[.[] | select(.tag_name | test("^openssl-3\\.5\\.[0-9]+$")) | .tag_name | ltrimstr("openssl-")] | sort_by(split(".") | map(tonumber)) | last' 2>/dev/null || true)"
      [[ -n "$version" && "$version" != "null" ]] && break
      echo "::warning::获取 OpenSSL 3.5 LTS 最新版本失败(第 ${attempt} 次), 10 秒后重试" >&2
      sleep 10
    done
    [[ -n "$version" && "$version" != "null" ]] || die "无法获取 OpenSSL 3.5 LTS 系列最新版本（连续 3 次失败, 请检查网络或稍后重试）"
    echo "未指定 openssl-version, 自动取 3.5 LTS 系列最新: $version" >&2
  fi

  printf '%s' "$version" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$' \
    || die "openssl-version 格式非法: ${version}（应为 X.Y.Z, 不带 v/openssl- 前缀）"
  printf '%s\n' "$version"
}

hash_file() {
  local file="$1"
  [[ -f "$file" ]] || die "待计算哈希的文件不存在: $file"
  local hash
  hash="$(sha256sum "$file")"
  hash="${hash%% *}"
  hash="${hash#\\}"
  [[ "$hash" =~ ^[0-9a-f]{64}$ ]] || die "sha256sum 输出非法: '$hash'"
  printf '%s\n' "$hash"
}

fetch_source() {
  local version="$1"
  local runner_temp="$2"
  local tarball="openssl-$version.tar.gz"
  cd "$runner_temp"
  rm -rf openssl-src "$tarball" "$tarball.sha256"
  curl --fail --location --retry 3 --retry-delay 5 \
    --output "$tarball" \
    "https://github.com/openssl/openssl/releases/download/openssl-$version/$tarball"
  curl --fail --location --retry 3 --retry-delay 5 \
    --output "$tarball.sha256" \
    "https://github.com/openssl/openssl/releases/download/openssl-$version/$tarball.sha256"
  sha256sum -c "$tarball.sha256"
  bash "$SCRIPT_DIR/verify-openssl-tarball.sh" "$version" "$tarball"
  mkdir -p openssl-src
  tar -xzf "$tarball" -C openssl-src --strip-components=1
}

verify_prefix() {
  local prefix="$1"
  local platform="$2"
  local source_hash="${3:-}"
  local lib_ssl="$prefix/lib/libssl.a"
  local lib_crypto="$prefix/lib/libcrypto.a"
  if [[ "$platform" == "windows" ]]; then
    lib_ssl="$prefix/lib/libssl.lib"
    lib_crypto="$prefix/lib/libcrypto.lib"
  fi
  for file in "$prefix/include/openssl/ssl.h" "$lib_ssl" "$lib_crypto"; do
    [[ -f "$file" ]] || die "缺少产物: $file"
  done
  local hash_file_path="$prefix/tarball.sha256"
  if [[ -n "$source_hash" ]]; then
    printf '%s\n' "$source_hash" > "$hash_file_path"
  fi
  local hash
  hash="$(cat "$hash_file_path" 2>/dev/null || true)"
  [[ "$hash" =~ ^[0-9a-f]{64}$ ]] || die "tarball.sha256 缺失或非法: '$hash'（编译/缓存异常, 可删除对应缓存后重试）"
  echo "产库产物齐备: ${prefix}（tarball SHA256: ${hash}）" >&2
  printf '%s\n' "$hash"
}

usage() {
  echo "用法: bash tools/ci/vendor-openssl-common.sh {resolve|fetch|hash|verify} ..." >&2
  exit 2
}

case "${1:-}" in
  resolve) shift; resolve_version "${1:-}" ;;
  fetch) shift; [[ "$#" -eq 2 ]] || usage; fetch_source "$1" "$2" ;;
  hash) shift; [[ "$#" -eq 1 ]] || usage; hash_file "$1" ;;
  verify) shift; [[ "$#" -ge 2 && "$#" -le 3 ]] || usage; verify_prefix "$1" "$2" "${3:-}" ;;
  *) usage ;;
esac
