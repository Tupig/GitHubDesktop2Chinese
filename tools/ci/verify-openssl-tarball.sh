#!/usr/bin/env bash
# =====================================================================
# verify-openssl-tarball.sh — OpenSSL 源码包 openssl.org 官方哈希独立比对(#184)
#
# 用法（CI 内由 .github/workflows/tupigghd2chinese.yml 四个产库任务下载
#      tarball 后调用；本地亦可手动运行）:
#   bash tools/ci/verify-openssl-tarball.sh <版本号> <tarball 路径>
#   例: bash tools/ci/verify-openssl-tarball.sh 3.5.9 openssl-3.5.9.tar.gz
#
# 背景(#184): 产库任务从 GitHub release 下载源码包并用同一 release 提供的
#   .sha256 旁证校验(#118)——tarball 与旁证同源, GitHub 平台被攻破时可被
#   同时篡改, 无独立防线。本脚本补充第二旁证, 与 openssl.org 官方公示哈希
#   独立比对:
#   1. 版本在下方常备映射表内 → 与硬编码的 openssl.org 官方 SHA256 比对;
#   2. 版本不在映射表（新版本刚发布）→ 在线抓取 openssl.org 官方 .sha256
#      交叉核对（openssl.org 自有 Azure Blob 直出, 与 GitHub 分属两源）;
#   3. 任一比对不通过, 或映射表未收录且 openssl.org 连续抓取失败 → 失败
#      退出（fail-closed）, 不允许无独立旁证通过。
#
# 映射表维护: 仅收录版本政策采纳的 3.5 LTS 系列（3.5 为 LTS 支持至 2030-04,
#   4.0 非 LTS 不采纳, 见工作流注7）; 上游发布新 patch 后请从
#   https://www.openssl.org/source/openssl-<版本>.tar.gz.sha256 获取官方哈希,
#   与 GitHub release .sha256 双源核验一致后补一行 case 条目, 并在 PR 描述
#   附来源链接与获取日期。
# =====================================================================
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "用法: bash tools/ci/verify-openssl-tarball.sh <版本号> <tarball 路径>" >&2
  exit 1
fi

OSSL_VER="$1"
TARBALL="$2"

# 版本格式守卫: 与工作流产库 resolve 步骤同式（#161 放宽后的 X.Y.Z 三段纯数字）
if ! printf '%s' "$OSSL_VER" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'; then
  echo "::error::openssl 版本格式非法: ${OSSL_VER}（应为 X.Y.Z, 不带 v/openssl- 前缀）" >&2
  exit 1
fi

if [ ! -f "$TARBALL" ]; then
  echo "::error::待校验源码包不存在: ${TARBALL}" >&2
  exit 1
fi

# ── 常备版本映射表(#184): openssl.org 官方公示 SHA256 ──
# 来源: https://www.openssl.org/source/openssl-<版本>.tar.gz.sha256
# （openssl.org 直出, 非 GitHub 重定向）; 每条均与 GitHub release .sha256
# 双源核验一致后登记; 获取日期: 2026-10-15
OSSL_OFFICIAL_SHA256=""
case "$OSSL_VER" in
  3.5.0) OSSL_OFFICIAL_SHA256="344d0a79f1a9b08029b0744e2cc401a43f9c90acd1044d09a530b4885a8e9fc0" ;;
  3.5.1) OSSL_OFFICIAL_SHA256="529043b15cffa5f36077a4d0af83f3de399807181d607441d734196d889b641f" ;;
  3.5.2) OSSL_OFFICIAL_SHA256="c53a47e5e441c930c3928cf7bf6fb00e5d129b630e0aa873b08258656e7345ec" ;;
  3.5.3) OSSL_OFFICIAL_SHA256="c9489d2abcf943cdc8329a57092331c598a402938054dc3a22218aea8a8ec3bf" ;;
  3.5.4) OSSL_OFFICIAL_SHA256="967311f84955316969bdb1d8d4b983718ef42338639c621ec4c34fddef355e99" ;;
  3.5.5) OSSL_OFFICIAL_SHA256="b28c91532a8b65a1f983b4c28b7488174e4a01008e29ce8e69bd789f28bc2a89" ;;
  3.5.6) OSSL_OFFICIAL_SHA256="deae7c80cba99c4b4f940ecadb3c3338b13cb77418409238e57d7f31f2a3b736" ;;
  3.5.7) OSSL_OFFICIAL_SHA256="a8c0d28a529ca480f9f36cf5792e2cd21984552a3c8e4aa11a24aa31aeac98e8" ;;
  3.5.8) OSSL_OFFICIAL_SHA256="a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2" ;;
  3.5.9) OSSL_OFFICIAL_SHA256="603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a" ;;
esac

# 本地实算哈希(#F2 同源处理: 截断到首个空格再剥离 '\' 前缀, 三平台统一纯哈希)
LOCAL_HASH="$(sha256sum "$TARBALL")"
LOCAL_HASH="${LOCAL_HASH%% *}"
LOCAL_HASH="${LOCAL_HASH#\\}"

# ── 路径1: 常备映射表命中 → 与硬编码 openssl.org 官方哈希比对 ──
if [ -n "$OSSL_OFFICIAL_SHA256" ]; then
  if [ "$LOCAL_HASH" = "$OSSL_OFFICIAL_SHA256" ]; then
    echo "openssl.org 官方哈希比对通过(#184, 来源: 常备映射表): $OSSL_VER → $LOCAL_HASH"
    exit 0
  fi
  echo "::error::openssl.org 官方哈希比对失败(#184): 版本 $OSSL_VER" >&2
  echo "  映射表(https://www.openssl.org/source/openssl-$OSSL_VER.tar.gz.sha256): $OSSL_OFFICIAL_SHA256" >&2
  echo "  本地实算: $LOCAL_HASH" >&2
  exit 1
fi

# ── 路径2: 映射表未收录 → 在线抓取 openssl.org 官方 .sha256 交叉核对 ──
# 下载文件名与 GitHub release 旁证(.sha256)区分开, 避免覆盖调用方已下载的同名旁证
OFFICIAL_FILE="openssl-$OSSL_VER.tar.gz.openssl-org.sha256"
FETCH_OK=""
for attempt in 1 2 3; do
  if curl --fail --location --retry 3 --retry-delay 5 \
      --output "$OFFICIAL_FILE" \
      "https://www.openssl.org/source/openssl-$OSSL_VER.tar.gz.sha256"; then
    FETCH_OK="true"
    break
  fi
  echo "::warning::抓取 openssl.org 官方 .sha256 失败(第 ${attempt} 次), 10 秒后重试" >&2
  sleep 10
done
if [ -z "$FETCH_OK" ]; then
  echo "::error::无法获取 openssl.org 官方哈希(#184): 版本 $OSSL_VER 未收录进常备映射表, 且 openssl.org 连续 3 次抓取失败——无独立旁证, 拒绝通过（新版本发布后请在 tools/ci/verify-openssl-tarball.sh 映射表登记官方哈希）" >&2
  exit 1
fi
# 官方 .sha256 行格式 "<哈希> *<文件名>": 取首字段并做 64 位十六进制守卫
OFFICIAL_HASH="$(awk 'NR==1{print $1}' "$OFFICIAL_FILE")"
if ! printf '%s' "$OFFICIAL_HASH" | grep -qE '^[0-9a-f]{64}$'; then
  echo "::error::openssl.org 官方 .sha256 内容非法(#184): '$OFFICIAL_HASH'" >&2
  exit 1
fi
if [ "$LOCAL_HASH" = "$OFFICIAL_HASH" ]; then
  echo "openssl.org 官方哈希比对通过(#184, 来源: 在线抓取 https://www.openssl.org/source/openssl-$OSSL_VER.tar.gz.sha256): $OSSL_VER → $LOCAL_HASH"
  echo "::notice::版本 $OSSL_VER 未收录进常备映射表, 建议在 tools/ci/verify-openssl-tarball.sh 补登记该版本官方哈希"
  exit 0
fi
echo "::error::openssl.org 官方哈希比对失败(#184): 版本 $OSSL_VER" >&2
echo "  openssl.org(https://www.openssl.org/source/openssl-$OSSL_VER.tar.gz.sha256): $OFFICIAL_HASH" >&2
echo "  本地实算: $LOCAL_HASH" >&2
exit 1
