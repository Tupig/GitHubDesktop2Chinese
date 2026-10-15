#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
源码编码格式质量门 v2（#88 基础 / #90 配置化增强）

规范：全部 git 跟踪的文本文件须为 UTF-8 编码且不带 BOM（ASCII 为 UTF-8 子集，视为合规）。
扫描：`git -c core.quotepath=false ls-files` —— 仅跟踪文件，构建产物（未跟踪）天然排除；
      中文文件名须关 quotepath 转义（#85 审计教训），否则会以八进制转义路径漏检。
违规：非白名单编码 / 意外 BOM 的文件逐个列出（::error:: 注解，CI 界面可点击定位），
      严格模式以退出码 1 使工作流失败；宽松模式（continue_on_violation）降级为
      ::warning:: 且退出码 0，用于存量治理/过渡期不阻断流水线。
例外：BOM 白名单内的文件允许携带 UTF-8 BOM（如 PowerShell 脚本兼容 Windows PowerShell 5.1）；
      若白名单文件丢失 BOM 则输出 ::warning::（编码仍合规，但本地兼容性受损，不阻断）。

配置三级合并（优先级从低到高）：
    1. 内置默认 DEFAULTS —— 不传任何参数时行为与 v1 完全一致（向后兼容）；
    2. 配置文件 --config（JSON）—— 显式给出的键覆盖默认值。CI 固定传入
       tools/ci/encoding-check.json 作为单一事实来源（改配置不必改脚本）；
    3. 命令行参数 —— --exclude/--allow 在前两级之上追加，--continue-on-violation/--strict
       覆盖宽松开关，--workers 覆盖并行度。
       注意：配置文件中的列表键为整体替换语义（写哪个键就以哪个为准，未写的键保留默认）。

性能：二进制扩展名在读文件前跳过；BOM/NUL 采样探测避免全量解码二进制；
      ThreadPoolExecutor 并行检测（workers=0 时自动取 min(32, CPU×4)）。

用法：
    python tools/ci/check-encoding.py                                           # 内置默认（= v1 行为）
    python tools/ci/check-encoding.py --config tools/ci/encoding-check.json     # CI 单一事实来源
    python tools/ci/check-encoding.py --exclude dir/ --allow gbk                # 追加排除/放行
    python tools/ci/check-encoding.py --continue-on-violation                   # 本次宽松运行
    python tools/ci/check-encoding.py --strict                                  # 强制严格（压过配置文件）

退出码：0 全部合规（或宽松模式放行）；1 存在违规（严格模式）；2 运行环境/配置错误。

编码背景（#85 审计 / #87 统一）：
    - JSON 带 BOM 违反 RFC 8259；.sh 带 BOM 破坏 shebang；
    - 本仓库 MSVC 已开 /utf-8 + 进程级 UTF-8 ACP，源码无 BOM 中文注释安全；
    - GBK 中文进入仓库会导致 Linux CI 乱码，故非 UTF-8 一律拦截。
"""

import argparse
import concurrent.futures
import functools
import io
import json
import os
import subprocess
import sys
from pathlib import Path

# 与 check-workflow.py / check-localization.py 统一: 守卫非 TextIOWrapper 的环境(#119/#247),
# 避免 Windows 控制台非 UTF-8 代码页下中文输出乱码
if isinstance(sys.stdout, io.TextIOWrapper):
    sys.stdout.reconfigure(encoding="utf-8")

# ── 内置默认配置（优先级最低，可被 --config 配置文件与命令行参数覆盖） ──
DEFAULTS = {
    # 允许编码白名单（检测标签小写；ascii 为 utf-8 子集，单独列出仅为统计展示）
    "allowed_encodings": ["utf-8", "ascii"],
    # 排除目录（前缀匹配，正斜杠）：第三方依赖随仓提交但编码不受本仓管控
    "exclude_dirs": ["third_party/"],
    # 允许携带 UTF-8 BOM 的文件（仓库根相对路径）：本地 Windows PowerShell 5.1 兼容
    "bom_exceptions": ["tools/ci/smoke-test.ps1"],
    # 已知二进制扩展名（跳过编码检测；UTF-16 BOM 检测先于此判断，不会漏检带 BOM 的 UTF-16 文本）
    "binary_extensions": [
        ".png", ".jpg", ".jpeg", ".gif", ".ico", ".icns", ".pdf", ".zip",
        ".gz", ".tar", ".7z", ".exe", ".dll", ".so", ".dylib", ".lib",
        ".a", ".o", ".obj", ".bin", ".woff", ".woff2", ".ttf", ".otf",
    ],
    # BOM/NUL 采样字节数（UTF-16 文本含 NUL，靠 BOM 检测先行区分）
    "sample_size": 8192,
    # 并行线程数；0 = 自动（min(32, CPU×4)）
    "workers": 0,
    # 宽松模式：违规仅 ::warning:: 且退出码 0（存量治理期过渡用）
    "continue_on_violation": False,
}

# 配置文件合法键 -> 期望类型（未知键 ::warning:: 忽略，防手滑拼错静默失效）
CONFIG_KEY_TYPES = {
    "allowed_encodings": list,
    "exclude_dirs": list,
    "bom_exceptions": list,
    "binary_extensions": list,
    "sample_size": int,
    "workers": int,
    "continue_on_violation": bool,
}


def detect_encoding(path: Path, sample_size: int = DEFAULTS["sample_size"]) -> str:
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
    if b"\x00" in raw[:sample_size]:
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


def load_config(path: str) -> dict:
    """读取并校验 JSON 配置文件，返回其键值（不与默认合并，由调用方 update）。"""
    try:
        raw = Path(path).read_text(encoding="utf-8-sig")  # 容忍误带 BOM 的手改配置
    except OSError as error:
        print(f"::error::无法读取配置文件 {path}: {error}")
        sys.exit(2)
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as error:
        print(f"::error::配置文件不是合法 JSON: {path}（第 {error.lineno} 行: {error.msg}）")
        sys.exit(2)
    if not isinstance(data, dict):
        print(f"::error::配置文件顶层须为 JSON 对象: {path}")
        sys.exit(2)
    for key, value in data.items():
        expected = CONFIG_KEY_TYPES.get(key)
        if expected is None:
            print(f"::warning::配置文件未知键 '{key}' 已忽略（{path}），请核对拼写")
            continue
        if not isinstance(value, expected) or (expected is int and isinstance(value, bool)):
            print(f"::error::配置键 '{key}' 类型应为 {expected.__name__}: {path}")
            sys.exit(2)
        if expected is list and not all(isinstance(item, str) for item in value):
            print(f"::error::配置键 '{key}' 的元素须为字符串: {path}")
            sys.exit(2)
    if data.get("sample_size", 1) <= 0 or data.get("workers", 0) < 0:
        print(f"::error::配置键取值非法（sample_size 须为正整数，workers 须为非负整数）: {path}")
        sys.exit(2)
    return data


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
    parser = argparse.ArgumentParser(
        description="源码编码格式质量门 v2（UTF-8 无 BOM；--config 配置化，默认行为与 v1 一致）")
    parser.add_argument("--config", metavar="FILE",
                        help="JSON 配置文件（显式给出的键覆盖内置默认；CI 使用 tools/ci/encoding-check.json）")
    parser.add_argument("--exclude", action="append", default=[], metavar="DIR",
                        help="追加排除目录（前缀匹配，可重复，如 --exclude build/）")
    parser.add_argument("--allow", action="append", default=[], metavar="ENC",
                        help="追加允许编码（可重复；默认白名单见 DEFAULTS/配置文件）")
    parser.add_argument("--continue-on-violation", action="store_true", default=None,
                        help="宽松模式：违规仅 ::warning:: 且退出码 0（本次运行生效）")
    parser.add_argument("--strict", action="store_false", dest="continue_on_violation", default=None,
                        help="强制严格模式（压过配置文件的宽松设置）")
    parser.add_argument("--workers", type=int, metavar="N",
                        help="并行线程数（0=自动；默认取配置文件值）")
    args = parser.parse_args()

    # ── 三级合并：内置默认 < 配置文件 < 命令行 ──
    cfg = dict(DEFAULTS)
    if args.config:
        cfg.update(load_config(args.config))
    allowed = {str(e).lower() for e in cfg["allowed_encodings"]} | {e.lower() for e in args.allow}
    exclude_dirs = tuple(str(e) for e in cfg["exclude_dirs"]) + tuple(args.exclude)
    bom_exceptions = {str(e) for e in cfg["bom_exceptions"]}
    binary_extensions = {str(e).lower() for e in cfg["binary_extensions"]}
    sample_size = int(cfg["sample_size"])
    lenient = bool(cfg["continue_on_violation"])
    if args.continue_on_violation is not None:
        lenient = args.continue_on_violation
    workers = args.workers if args.workers is not None else int(cfg["workers"])

    files = list_tracked_files()
    tasks: list[str] = []      # 待检测文件（正斜杠相对路径，已过目录/扩展名预过滤）
    excluded: list[str] = []
    binaries: list[str] = []
    for rel in files:
        norm = rel.replace("\\", "/")
        if norm.startswith(exclude_dirs):
            excluded.append(norm)
            continue
        if Path(norm).suffix.lower() in binary_extensions:
            binaries.append(norm)
            continue
        tasks.append(norm)

    # ── 并行检测（I/O 密集；pool.map 保持结果与 tasks 同序，输出顺序稳定） ──
    if workers <= 0:
        workers = min(32, (os.cpu_count() or 4) * 4)
    worker_count = max(1, min(workers, len(tasks) or 1))
    detect = functools.partial(detect_encoding, sample_size=sample_size)
    with concurrent.futures.ThreadPoolExecutor(max_workers=worker_count) as pool:
        encodings = list(pool.map(detect, [Path(name) for name in tasks]))

    # ── 分类（判定逻辑与 v1 完全一致） ──
    stats: dict[str, int] = {}
    violations: list[tuple[str, str]] = []
    bom_ok: list[str] = []
    warnings: list[str] = []
    for norm, encoding in zip(tasks, encodings):
        stats[encoding] = stats.get(encoding, 0) + 1
        if encoding == "binary":
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

    # ── 输出（合规文件不逐个刷屏，仅例外/告警/违规与统计） ──
    print("== 源码编码格式检查（规范: UTF-8 无 BOM）==")
    if args.config:
        print(f"配置文件: {args.config}")
    print(f"模式: {'宽松（违规仅告警，不阻断）' if lenient else '严格（违规即失败）'}")
    if worker_count > 1:
        print(f"并行: {worker_count} workers")
    print(f"扫描: {len(files)} 个 git 跟踪文件, 排除 {len(excluded)} 个, 二进制跳过 {len(binaries)} 个")
    if exclude_dirs:
        print(f"排除目录: {', '.join(exclude_dirs)}")
    print(f"编码白名单: {', '.join(sorted(allowed))}")
    if bom_exceptions:
        print(f"BOM 例外: {', '.join(sorted(bom_exceptions))}")
    for name in bom_ok:
        print(f"[例外允许 BOM] {name}")
    for name in warnings:
        print(f"::warning::BOM 例外文件 {name} 未检测到 UTF-8 BOM（本地 Windows PowerShell 5.1 运行会乱码，建议补回 BOM）")
    allowed_hint = ", ".join(sorted(allowed))
    for name, encoding in violations:
        if lenient:
            print(f"::warning::编码违规(宽松模式): {name} -> 检测到 {encoding}（允许: {allowed_hint}）")
        else:
            print(f"::error::编码违规: {name} -> 检测到 {encoding}（允许: {allowed_hint}）")

    distribution = ", ".join(f"{stats[k]} {k}" for k in sorted(stats) if k != "binary") or "0"
    text_scanned = len(tasks) - sum(1 for e in encodings if e == "binary")
    print(f"== 结果: 扫描 {text_scanned} 个文本文件（编码分布: {distribution}）, 违规 {len(violations)} 个 ==")

    if violations:
        if lenient:
            print(f"::warning::宽松模式: {len(violations)} 个编码违规文件未阻断本次运行（见上方清单），请尽快转为 UTF-8 无 BOM")
            return 0
        print("::error::存在编码违规文件（见上方清单），须转换为 UTF-8 无 BOM 后重新提交")
        return 1
    print("全部合规。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
