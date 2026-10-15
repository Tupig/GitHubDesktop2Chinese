#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check-encoding.py 的单元测试。

被测模块导入无副作用（主流程收在 main() 且有 __main__ 守卫），可直接经
importlib 加载后测试。覆盖：detect_encoding 的 BOM/二进制采样/各编码探测、
load_config 的类型/未知键/取值边界与退出码 2 路径、DEFAULTS 与
CONFIG_KEY_TYPES 的表面一致性、以及临时 git 仓库内 main() 的三级配置
合并/违规判定/宽松与严格模式端到端行为。

用法：
    python tools/ci/check-encoding-test.py       # 运行全部用例
    python tools/ci/check-encoding-test.py -v    # 逐用例输出
"""

from __future__ import annotations

import importlib.util
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

# 通过文件路径加载被测模块（文件名含连字符，无法常规 import）
MODULE_PATH = Path(__file__).resolve().parent / "check-encoding.py"
SPEC = importlib.util.spec_from_file_location("check_encoding", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"无法加载被测模块: {MODULE_PATH}")
check_encoding = importlib.util.module_from_spec(SPEC)
sys.modules["check_encoding"] = check_encoding
SPEC.loader.exec_module(check_encoding)


class DetectEncodingTest(unittest.TestCase):
    """单文件编码探测（BOM 探测 -> 二进制采样 -> 严格解码 -> 遗留编码回退）。"""

    def setUp(self) -> None:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.base_dir = Path(tmp.name)

    def detect(self, blob: bytes, **kwargs) -> str:
        path = self.base_dir / "sample.txt"
        path.write_bytes(blob)
        self.addCleanup(path.unlink)
        return check_encoding.detect_encoding(path, **kwargs)

    def test_empty_file_is_ascii(self) -> None:
        self.assertEqual(self.detect(b""), "ascii")

    def test_pure_ascii(self) -> None:
        self.assertEqual(self.detect(b"plain text\n"), "ascii")

    def test_utf8_chinese(self) -> None:
        self.assertEqual(self.detect("中文内容".encode("utf-8")), "utf-8")

    def test_utf8_bom(self) -> None:
        self.assertEqual(self.detect(b"\xef\xbb\xbf" + "中文".encode()), "utf-8-bom")

    def test_utf16_le_bom(self) -> None:
        blob = b"\xff\xfe" + "中文".encode("utf-16-le")
        self.assertEqual(self.detect(blob), "utf-16-le")

    def test_utf16_be_bom(self) -> None:
        blob = b"\xfe\xff" + "中文".encode("utf-16-be")
        self.assertEqual(self.detect(blob), "utf-16-be")

    def test_utf32_bom_reported_as_utf16_be(self) -> None:
        # UTF-32 BOM 目前归入 utf-16-be 标签（均不在白名单, 判定不受影响）
        blob = b"\x00\x00\xfe\xff" + "ab".encode("utf-32-le")
        self.assertEqual(self.detect(blob), "utf-16-be")

    def test_gbk(self) -> None:
        self.assertEqual(self.detect("中文内容".encode("gbk")), "gbk")

    def test_shift_jis_halfwidth_katakana(self) -> None:
        # 0xB1(半角片假名)不在 GBK 单字节/双字节合法形态中, 落到 shift_jis
        self.assertEqual(self.detect(b"\xb1"), "shift_jis")

    def test_latin1(self) -> None:
        # 0x80 在 GBK/Big5/Shift_JIS 均非法, 由 latin-1 兜底
        self.assertEqual(self.detect(b"\x80"), "latin-1")

    def test_binary_with_nul(self) -> None:
        self.assertEqual(self.detect(b"abc\x00def"), "binary")

    def test_bomless_utf16le_treated_as_binary(self) -> None:
        # 无 BOM 的 UTF-16LE 文本含 NUL, 采样探测按二进制跳过
        self.assertEqual(self.detect("ab".encode("utf-16-le")), "binary")

    def test_nul_beyond_sample_not_binary(self) -> None:
        # NUL 落在采样窗口之外时不判为二进制（NUL 本身是合法 UTF-8 字节）
        blob = b"abcd" + b"\x00" * 16
        self.assertEqual(self.detect(blob, sample_size=4), "ascii")

    def test_nul_within_sample_is_binary(self) -> None:
        blob = b"ab\x00cd"
        self.assertEqual(self.detect(blob, sample_size=8), "binary")

    def test_read_error_reported(self) -> None:
        # 目录路径 read_bytes 抛 OSError, 返回 read-error(...) 而非崩溃
        result = check_encoding.detect_encoding(self.base_dir)
        self.assertTrue(result.startswith("read-error("), result)


class LoadConfigTest(unittest.TestCase):
    """配置文件加载校验（类型/未知键/取值边界, 错误路径退出码 2）。"""

    def setUp(self) -> None:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.base_dir = Path(tmp.name)

    def write_config(self, payload, *, raw: bool = False) -> Path:
        path = self.base_dir / "config.json"
        if raw:
            path.write_text(payload, encoding="utf-8")
        else:
            path.write_text(json.dumps(payload, ensure_ascii=False),
                            encoding="utf-8")
        return path

    def test_valid_config_returned(self) -> None:
        payload = {"allowed_encodings": ["utf-8"], "workers": 4,
                   "continue_on_violation": False}
        with redirect_stdout(io.StringIO()):
            result = check_encoding.load_config(str(self.write_config(payload)))
        self.assertEqual(result, payload)

    def test_bom_tolerated(self) -> None:
        # 手改配置误带 BOM 时以 utf-8-sig 读取, 不报错
        path = self.base_dir / "config.json"
        path.write_bytes(b"\xef\xbb\xbf" + json.dumps({"workers": 2}).encode())
        with redirect_stdout(io.StringIO()):
            result = check_encoding.load_config(str(path))
        self.assertEqual(result, {"workers": 2})

    def test_missing_file_exits_2(self) -> None:
        output = io.StringIO()
        with redirect_stdout(output):
            with self.assertRaises(SystemExit) as ctx:
                check_encoding.load_config(str(self.base_dir / "不存在.json"))
        self.assertEqual(ctx.exception.code, 2)
        self.assertIn("::error::无法读取配置文件", output.getvalue())

    def test_invalid_json_exits_2(self) -> None:
        output = io.StringIO()
        with redirect_stdout(output):
            with self.assertRaises(SystemExit) as ctx:
                check_encoding.load_config(str(self.write_config("{oops", raw=True)))
        self.assertEqual(ctx.exception.code, 2)
        self.assertIn("配置文件不是合法 JSON", output.getvalue())

    def test_top_level_not_object_exits_2(self) -> None:
        output = io.StringIO()
        with redirect_stdout(output):
            with self.assertRaises(SystemExit) as ctx:
                check_encoding.load_config(
                    str(self.write_config('["a"]', raw=True)))
        self.assertEqual(ctx.exception.code, 2)
        self.assertIn("顶层须为 JSON 对象", output.getvalue())

    def test_unknown_key_warned_and_not_validated(self) -> None:
        # 未知键打 ::warning:: 且跳过类型校验; 返回值仍原样携带该键
        # (main() 合并进 cfg 后无人消费, 无行为影响)
        payload = {"workres": 4, "workers": 8}  # workres 为拼错键
        output = io.StringIO()
        with redirect_stdout(output):
            result = check_encoding.load_config(str(self.write_config(payload)))
        self.assertEqual(result, {"workres": 4, "workers": 8})
        self.assertIn("::warning::配置文件未知键 'workres' 已忽略",
                      output.getvalue())

    def test_key_type_mismatches_exit_2(self) -> None:
        # 表驱动: 每个合法键给一个类型错误的值(bool 对 int 键同样非法)
        table = [
            ("allowed_encodings", "utf-8"),
            ("exclude_dirs", 3),
            ("bom_exceptions", {}),
            ("binary_extensions", ".png"),
            ("sample_size", "8192"),
            ("sample_size", True),
            ("workers", False),
            ("continue_on_violation", "true"),
        ]
        for key, value in table:
            with self.subTest(key=key, value=value):
                output = io.StringIO()
                with redirect_stdout(output):
                    with self.assertRaises(SystemExit) as ctx:
                        check_encoding.load_config(
                            str(self.write_config({key: value})))
                self.assertEqual(ctx.exception.code, 2)
                self.assertIn(f"配置键 '{key}' 类型应为", output.getvalue())

    def test_list_element_not_string_exits_2(self) -> None:
        output = io.StringIO()
        with redirect_stdout(output):
            with self.assertRaises(SystemExit) as ctx:
                check_encoding.load_config(
                    str(self.write_config({"allowed_encodings": [1, 2]})))
        self.assertEqual(ctx.exception.code, 2)
        self.assertIn("元素须为字符串", output.getvalue())

    def test_value_boundaries_exit_2(self) -> None:
        # 表驱动: 取值边界（sample_size 须为正, workers 须为非负）
        table = [
            {"sample_size": 0},
            {"sample_size": -1},
            {"workers": -1},
        ]
        for payload in table:
            with self.subTest(payload=payload):
                output = io.StringIO()
                with redirect_stdout(output):
                    with self.assertRaises(SystemExit) as ctx:
                        check_encoding.load_config(
                            str(self.write_config(payload)))
                self.assertEqual(ctx.exception.code, 2)
                self.assertIn("取值非法", output.getvalue())

    def test_minimal_boundary_values_accepted(self) -> None:
        payload = {"sample_size": 1, "workers": 0}
        with redirect_stdout(io.StringIO()):
            result = check_encoding.load_config(str(self.write_config(payload)))
        self.assertEqual(result, payload)


class ConfigSurfaceTest(unittest.TestCase):
    """内置默认与合法键表面一致性（防新增配置键漏挂类型校验）。"""

    def test_defaults_and_key_types_aligned(self) -> None:
        self.assertEqual(set(check_encoding.DEFAULTS),
                         set(check_encoding.CONFIG_KEY_TYPES))

    def test_key_types_match_default_value_types(self) -> None:
        for key, expected in check_encoding.CONFIG_KEY_TYPES.items():
            with self.subTest(key=key):
                self.assertIsInstance(check_encoding.DEFAULTS[key], expected)


class MainIntegrationTest(unittest.TestCase):
    """临时 git 仓库内端到端运行 main()（三级配置合并/违规判定/退出码）。"""

    def setUp(self) -> None:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.repo = Path(tmp.name)
        subprocess.run(["git", "init", "-q"], cwd=self.repo, check=True)
        old_cwd = os.getcwd()
        self.addCleanup(os.chdir, old_cwd)
        os.chdir(self.repo)

    def stage(self, rel: str, blob: bytes) -> None:
        path = self.repo / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(blob)
        subprocess.run(["git", "add", "--", rel], cwd=self.repo, check=True)

    def run_main(self, *argv: str) -> tuple:
        output = io.StringIO()
        with mock.patch.object(sys, "argv", ["check-encoding.py", *argv]):
            with redirect_stdout(output):
                code = check_encoding.main()
        return code, output.getvalue()

    def write_config(self, payload) -> str:
        path = self.repo / "cfg.json"
        path.write_text(json.dumps(payload, ensure_ascii=False),
                        encoding="utf-8")
        return str(path)

    def test_clean_repo_passes(self) -> None:
        self.stage("a.txt", "中文内容\n".encode("utf-8"))
        code, output = self.run_main()
        self.assertEqual(code, 0)
        self.assertIn("全部合规", output)
        self.assertIn("模式: 严格", output)

    def test_gbk_violation_fails(self) -> None:
        self.stage("gbk.txt", "中文内容\n".encode("gbk"))
        code, output = self.run_main()
        self.assertEqual(code, 1)
        self.assertIn("::error::编码违规: gbk.txt -> 检测到 gbk", output)

    def test_bom_violation_fails(self) -> None:
        self.stage("bom.txt", b"\xef\xbb\xbf" + "中文".encode())
        code, output = self.run_main()
        self.assertEqual(code, 1)
        self.assertIn("::error::编码违规: bom.txt -> 检测到 utf-8-bom", output)

    def test_lenient_flag_downgrades_to_warning(self) -> None:
        self.stage("gbk.txt", "中文内容\n".encode("gbk"))
        code, output = self.run_main("--continue-on-violation")
        self.assertEqual(code, 0)
        self.assertIn("::warning::编码违规(宽松模式)", output)
        self.assertIn("未阻断本次运行", output)

    def test_config_lenient_and_cli_strict_override(self) -> None:
        # 三级合并: 配置文件宽松 -> 退出码 0; 命令行 --strict 压过配置 -> 1
        self.stage("gbk.txt", "中文内容\n".encode("gbk"))
        config = self.write_config({"continue_on_violation": True})
        code, _ = self.run_main("--config", config)
        self.assertEqual(code, 0)
        code, output = self.run_main("--config", config, "--strict")
        self.assertEqual(code, 1)
        self.assertIn("模式: 严格", output)

    def test_bom_exception_allowed_via_config(self) -> None:
        self.stage("bom.txt", b"\xef\xbb\xbf" + "中文".encode())
        config = self.write_config({"bom_exceptions": ["bom.txt"]})
        code, output = self.run_main("--config", config)
        self.assertEqual(code, 0)
        self.assertIn("[例外允许 BOM] bom.txt", output)

    def test_bom_exception_lost_bom_warns_only(self) -> None:
        # 白名单文件丢失 BOM: 编码仍合规, 仅 ::warning:: 不阻断
        self.stage("ps.ps1", "Write-Host ok\n".encode("utf-8"))
        config = self.write_config({"bom_exceptions": ["ps.ps1"]})
        code, output = self.run_main("--config", config)
        self.assertEqual(code, 0)
        self.assertIn("::warning::BOM 例外文件 ps.ps1 未检测到 UTF-8 BOM", output)

    def test_exclude_dir_via_config(self) -> None:
        self.stage("skip/x.txt", "中文内容\n".encode("gbk"))
        self.stage("keep.txt", "正常\n".encode("utf-8"))
        config = self.write_config({"exclude_dirs": ["skip/"]})
        code, output = self.run_main("--config", config)
        self.assertEqual(code, 0)
        self.assertIn("排除目录: skip/", output)

    def test_cli_exclude_appends_to_config(self) -> None:
        self.stage("skip/x.txt", "中文内容\n".encode("gbk"))
        self.stage("more/y.txt", "中文内容\n".encode("gbk"))
        config = self.write_config({"exclude_dirs": ["skip/"]})
        code, _ = self.run_main("--config", config, "--exclude", "more/")
        self.assertEqual(code, 0)

    def test_allow_flag_admits_encoding(self) -> None:
        self.stage("gbk.txt", "中文内容\n".encode("gbk"))
        code, output = self.run_main("--allow", "GBK")  # 大小写不敏感
        self.assertEqual(code, 0)
        self.assertIn("编码白名单: ascii, gbk, utf-8", output)

    def test_binary_extension_skipped_before_read(self) -> None:
        self.stage("img.png", b"\x89PNG\r\n\x1a\n\x00\x00binary")
        code, _ = self.run_main()
        self.assertEqual(code, 0)

    def test_default_third_party_excluded(self) -> None:
        # 内置默认排除 third_party/, 不传配置同样生效
        self.stage("third_party/x.txt", "中文内容\n".encode("gbk"))
        code, _ = self.run_main()
        self.assertEqual(code, 0)

    def test_untracked_files_not_scanned(self) -> None:
        # git ls-files 仅列跟踪文件, 未 add 的违规文件天然排除
        (self.repo / "untracked.txt").write_bytes("中文内容\n".encode("gbk"))
        code, _ = self.run_main()
        self.assertEqual(code, 0)


if __name__ == "__main__":
    unittest.main()
