#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
源码编码格式质量门（#88）

规范：全部 git 跟踪的文本文件须为 UTF-8 编码且不带 BOM（ASCII 为 UTF-8 子集，视为合规）。
扫描：`git -c core.quotepath=false ls-files` —— 仅跟踪文件，构建产物（未跟踪）天然排除；
      中文文件名须关 quotepath 转义（#85 审计教训），否则会以八进制转义路径漏检。
违规：非 UTF-8 编码 / 意外 BOM 的文件逐个列出（::error:: 注解，CI 界面可点击定位），
      并以退出码 1 使工作流失败。
例外：BOM 白名单内的文件允许携带 UTF-8 BOM（如 PowerShell 脚本兼容 Windows PowerShell 5.1）；
      若白名单文件丢失 BOM 则输出 ::warning::（编码仍合规，但本地兼容性受损，不阻断）。

用法：
    python tools/ci/check-encoding.py                 # 按 CONFIG 默认配置全量扫描
    python tools/ci/check-encoding.py --exclude dir/  # 追加排除目录（可重复）
    python tools/ci/check-encoding.py --allow gbk     # 追加允许编码（不建议）

编码背景（#85 审计 / #87 统一）：
    - JSON 带 BOM 违反 RFC 8259；.sh 带 BOM 破坏 shebang；
    - 本仓库 MSVC 已开 /utf-8 + 进程级 UTF-8 ACP，源码无 BOM 中文注释安全；
    - GBK 中文进入仓库会导致 Linux CI 乱码，故非 UTF-8 一律拦截。
"""

import argparse
import subprocess
import sys
from pathlib import Path

# ── 配置区：允许的编码白名单 / 排除目录 / BOM 例外（按需调整，其余默认） ──
CONFIG = {
    # 允许编码白名单（检测标签小写；ascii 为 utf-8 子集，单独列出仅为统计展示）
    "allowed_encodings": {"utf-8", "ascii"},
    # 排除目录（前缀匹配，正斜杠）：第三方依赖随仓提交但编码不受本仓管控
    "exclude_dirs": ("third_party/",),
    # 允许携带 UTF-8 BOM 的文件（须为仓库根相对路径）：本地 Windows PowerShell 5.1 兼容
    "bom_exceptions": {"tools/ci/smoke-test.ps1"},
    # 已知二进制扩展名（跳过编码检测；UTF-16 BOM 检测先于此判断，不会漏检带 BOM 的 UTF-16 文本）
    "binary_extensions": {
        ".png", ".jpg", ".jpeg", ".gif", ".ico", ".icns", ".pdf", ".zip",
        ".gz", ".tar", ".7z", ".exe", ".dll", ".so", ".dylib", ".lib",
        ".a", ".o", ".obj", ".bin", ".woff", ".woff2", ".ttf", ".otf",
    },
}

SAMPLE_SIZE = 8192  # 二进制探测的采样字节数（UTF-16 文本含 NUL，靠 BOM 检测先行区分）


def detect_encoding(path: Path) -> str:
    """检测单个文件的实际编码，返回检测标签（小写）。"""
    try:
        raw = path.read_bytes()
    except OSError as error:
        return f"read-error({error})"

    # 1) BOM 探测（先于二进制判断：UTF-16 文本的 ASCII 字符会引入 NUL 字节）
    if raw.startswith(b"\xef\xbb\xbf"):
        return "utf-8-bom"
    if raw.startswith(b"\xff\xfe"):
        return "utf-16-le"
    if raw.startswith(b"\xfe\xff") or raw.startswith(b"\x00\x00\xfe\xff"):
        return "utf-16-be"

    # 2) 二进制探测：采样区含 NUL 视为二进制（BOM-less UTF-16 无从区分真实二进制，按二进制跳过）
    if b"\x00" in raw[:SAMPLE_SIZE]:
        return "binary"

    # 3) UTF-8 严格解码（ASCII 自然通过）
    try:
        raw.decode("utf-8", errors="strict")
        if raw.isascii():
            return "ascii"
        return "utf-8"
    except UnicodeDecodeError:
        pass

    # 4) 常见遗留编码回退（仅为报告"检测到的编码"，不在白名单即违规）
    for encoding in ("gbk", "big5", "shift_jis", "latin-1"):
        try:
            raw.decode(encoding, errors="strict")
            return encoding
        except UnicodeDecodeError:
            continue
    return "unknown"


def list_tracked_files() -> list[str]:
    """列出 git 跟踪文件（关 quotepath 转义保证中文文件名原样输出）。"""
    result = subprocess.run(
        ["git", "-c", "core.quotepath=false", "ls-files"],
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        print(f"::error::git ls-files 失败: {result.stderr.decode('utf-8', errors='replace').strip()}")
        sys.exit(2)
    return [line for line in result.stdout.decode("utf-8", errors="replace").splitlines() if line]


def main() -> int:
    parser = argparse.ArgumentParser(description="源码编码格式质量门（UTF-8 无 BOM）")
    parser.add_argument(
        "--exclude", action="append", default=[], metavar="DIR",
        help="追加排除目录（前缀匹配，可重复，如 --exclude build/）",
    )
    parser.add_argument(
        "--allow", action="append", default=[], metavar="ENC",
        help="追加允许编码（可重复；默认白名单见 CONFIG）",
    )
    args = parser.parse_args()

    allowed = set(CONFIG["allowed_encodings"]) | {e.lower() for e in args.allow}
    exclude_dirs = tuple(CONFIG["exclude_dirs"]) + tuple(args.exclude)
    bom_exceptions = set(CONFIG["bom_exceptions"])

    files = list_tracked_files()
    excluded, binaries, stats = [], [], {}
    violations: list[tuple[str, str]] = []
    bom_ok, warnings = [], []

    for rel in files:
        # 排除目录（统一正斜杠前缀匹配）
        norm = rel.replace("\\", "/")
        if norm.startswith(exclude_dirs):
            excluded.append(norm)
            continue
        path = Path(norm)
        if path.suffix.lower() in CONFIG["binary_extensions"]:
            binaries.append(norm)
            continue
        encoding = detect_encoding(path)
        stats[encoding] = stats.get(encoding, 0) + 1
        if encoding in ("binary",):
            binaries.append(norm)
        elif encoding == "utf-8-bom":
            if norm in bom_exceptions:
                bom_ok.append(norm)
            else:
                violations.append((norm, encoding))
        elif encoding not in allowed:
            violations.append((norm, encoding))
        # BOM 例外文件丢失 BOM：编码仍合规，但本地 PowerShell 5.1 兼容受损，仅告警
        if norm in bom_exceptions and encoding != "utf-8-bom" and encoding != "binary":
            warnings.append(norm)

    # ── 输出（合规文件不逐个刷屏，仅异常与统计） ──
    print("== 源码编码格式检查（规范: UTF-8 无 BOM）==")
    print(f"扫描: {len(files)} 个 git 跟踪文件")
    if exclude_dirs:
        print(f"排除目录: {', '.join(exclude_dirs)}")
    print(f"编码白名单: {', '.join(sorted(allowed))}")
    if bom_exceptions:
        print(f"BOM 例外: {', '.join(sorted(bom_exceptions))}")
    if binaries:
        print(f"二进制跳过: {len(binaries)} 个")
    for name in bom_ok:
        print(f"[例外允许 BOM] {name}")
    for name in warnings:
        print(f"::warning::BOM 例外文件 {name} 未检测到 UTF-8 BOM（本地 Windows PowerShell 5.1 运行会乱码，建议补回 BOM）")
    for name, encoding in violations:
        print(f"::error::编码违规: {name} -> 检测到 {encoding}（允许: {', '.join(sorted(allowed))}）")

    summary = ", ".join(f"{stats[k]} {k}" for k in sorted(stats) if k != "binary") or "0"
    print(f"== 结果: 扫描 {len(files) - len(excluded) - len(binaries)} 个文本文件（{summary}）, 违规 {len(violations)} 个 ==")

    if violations:
        print("::error::存在编码违规文件（见上方清单），须转换为 UTF-8 无 BOM 后重新提交")
        return 1
    print("全部合规。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
