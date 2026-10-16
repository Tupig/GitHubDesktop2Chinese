#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
md-check.py 的单元测试。

被测脚本按 __file__ 相对路径推导仓库根并立即扫描全部 md 文件——因此将
脚本副本放入临时目录夹具后经 importlib 执行，即可在不修改校验器本体的
前提下，让模块级全流程（含 sys.exit 退出码与统计输出）完整跑在夹具文件上；
slugify/add_anchor/scan_markdown 为可导入纯函数，直接对加载后的模块实例
做表驱动测试。

覆盖：slugify（大小写/标点剔除/空格不折叠/中文/emoji 标题）/ add_anchor
（同名 -1/-2 后缀、空锚点忽略）/ scan_markdown（标题锚点、闭合 #、行内
代码、围栏 ``` 与 ~~~、单行/跨行 HTML 注释、<a id> 标签、行内代码剔除、
非 UTF-8 跳过、BOM 容忍）/ 端到端（同页与跨文件锚点、重复标题 -1、外部
与绝对路径跳过、Wiki 无扩展名、%20 编码、含空格目标、空目标、PRUNE_DIRS
剪枝、统计与退出码）。

用法：
    python tools/ci/md-check-test.py       # 运行全部用例
    python tools/ci/md-check-test.py -v    # 逐用例输出
"""

from __future__ import annotations

import importlib.util
import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

SCRIPT_PATH = Path(__file__).resolve().parent / "md-check.py"

# 校验器支持的扫描根覆盖环境变量（#263）：测试据此把扫描根指向夹具目录，
# 从而可直接加载真实脚本（不再复制副本），覆盖率可归因到本源文件。
ROOT_ENV = "GHD2C_CHECK_ROOT"

_module_counter = 0


def load_md_check(md_files: dict | None = None,
                  raw_files: dict | None = None) -> tuple:
    """在临时根目录夹具上加载并执行**真实**校验器脚本，返回 (module, exit_code, output)。

    md_files: 相对路径 -> 文本内容（UTF-8 写入）
    raw_files: 相对路径 -> 原始字节（非 UTF-8 / BOM 用例）
    exit_code 为 0 表示模块正常执行完毕；sys.exit(1) 被捕获为 1。
    """
    global _module_counter
    _module_counter += 1
    tmp = tempfile.TemporaryDirectory()
    root = Path(tmp.name)
    for rel, text in (md_files or {}).items():
        target = root / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")
    for rel, blob in (raw_files or {}).items():
        target = root / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(blob)
    spec = importlib.util.spec_from_file_location(
        f"md_check_fixture_{_module_counter}",
        SCRIPT_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"无法加载被测模块: {SCRIPT_PATH}")
    module = importlib.util.module_from_spec(spec)
    output = io.StringIO()
    exit_code = 0
    prev_root = os.environ.get(ROOT_ENV)
    os.environ[ROOT_ENV] = str(root)
    try:
        with redirect_stdout(output):
            spec.loader.exec_module(module)
    except SystemExit as exc:
        exit_code = exc.code
    finally:
        if prev_root is None:
            os.environ.pop(ROOT_ENV, None)
        else:
            os.environ[ROOT_ENV] = prev_root
        tmp.cleanup()
    return module, exit_code, output.getvalue()


class MdCheckTestCase(unittest.TestCase):
    """公共夹具：空根目录加载一次模块实例供纯函数测试复用。"""

    @classmethod
    def setUpClass(cls) -> None:
        cls.module, _, _ = load_md_check()
        if cls.module is None:
            raise RuntimeError("无法加载被测模块")

    def setUp(self) -> None:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.base_dir = Path(tmp.name)

    def write_md(self, text: str, name: str = "fixture.md") -> Path:
        path = self.base_dir / name
        path.write_text(text, encoding="utf-8")
        return path


class SlugifyTest(MdCheckTestCase):
    """GitHub 风格标题锚点规则（表驱动）。"""

    def test_slugify_table(self) -> None:
        table = [
            ("Hello World", "hello-world"),
            ("Hello, World!", "hello-world"),       # 标点剔除
            ("构建指南", "构建指南"),                 # 中文为 \w 保留
            ("a  b", "a--b"),                       # 空格逐个转连字符, 不折叠
            ("a_b-c", "a_b-c"),                     # 下划线/连字符保留
            ("`code` 标题", "code-标题"),             # 反引号剔除(内容保留)
            ("🧪 CI / 自动维护", "-ci--自动维护"),     # emoji 剔除, 其后空格保留
            ("A B C", "a-b-c"),                     # 小写化
        ]
        for text, expected in table:
            with self.subTest(text=text):
                self.assertEqual(self.module.slugify(text), expected)


class AddAnchorTest(MdCheckTestCase):
    """同名标题按 GitHub 规则追加 -1/-2 后缀。"""

    def test_first_anchor_added(self) -> None:
        anchors: set = set()
        self.module.add_anchor(anchors, "x")
        self.assertEqual(anchors, {"x"})

    def test_duplicate_anchors_numbered(self) -> None:
        anchors: set = set()
        for _ in range(3):
            self.module.add_anchor(anchors, "x")
        self.assertEqual(anchors, {"x", "x-1", "x-2"})

    def test_empty_anchor_ignored(self) -> None:
        anchors: set = set()
        self.module.add_anchor(anchors, "")
        self.assertEqual(anchors, set())

    def test_numbering_fills_gap(self) -> None:
        # x 与 x-1 已占用时, 第三个同名标题落到 x-2 而非 x-3
        anchors: set = {"x", "x-1"}
        self.module.add_anchor(anchors, "x")
        self.assertIn("x-2", anchors)


class ScanMarkdownTest(MdCheckTestCase):
    """单文件解析：锚点提取与链接提取。"""

    def scan(self, text: str) -> tuple:
        return self.module.scan_markdown(self.write_md(text))

    def test_headings_produce_anchors(self) -> None:
        anchors, links = self.scan("# 一级\n## 二级\n### 三级\n正文")
        self.assertEqual(anchors, {"一级", "二级", "三级"})
        self.assertEqual(links, [])

    def test_heading_indented_up_to_three_spaces(self) -> None:
        anchors, _ = self.scan("   # 缩进标题")
        self.assertEqual(anchors, {"缩进标题"})

    def test_four_space_indent_not_heading(self) -> None:
        anchors, _ = self.scan("    # 代码块标题")
        self.assertEqual(anchors, set())

    def test_closing_hashes_stripped(self) -> None:
        anchors, _ = self.scan("## 标题 ##")
        self.assertEqual(anchors, {"标题"})

    def test_inline_code_in_heading_kept_without_backticks(self) -> None:
        anchors, _ = self.scan("## `code` 标题")
        self.assertEqual(anchors, {"code-标题"})

    def test_duplicate_headings_numbered(self) -> None:
        anchors, _ = self.scan("## 同名\n## 同名\n## 同名")
        self.assertEqual(anchors, {"同名", "同名-1", "同名-2"})

    def test_anchor_tag_registered(self) -> None:
        text = '<a id="custom"></a>\n<a name=\'n2\'></a>\n'
        anchors, _ = self.scan(text)
        self.assertEqual(anchors, {"custom", "n2"})

    def test_links_extracted_with_lineno(self) -> None:
        _, links = self.scan("正文\n\n[链接](target.md)")
        self.assertEqual(links, [(3, "target.md")])

    def test_fence_skips_content(self) -> None:
        text = "```\n[围栏](missing.md)\n# 非标题\n```\n# 真标题\n[真](ok.md)"
        anchors, links = self.scan(text)
        self.assertEqual(anchors, {"真标题"})
        self.assertEqual(links, [(6, "ok.md")])

    def test_tilde_fence_skips_content(self) -> None:
        text = "~~~\n[围栏](missing.md)\n~~~\n[真](ok.md)"
        anchors, links = self.scan(text)
        self.assertEqual(anchors, set())
        self.assertEqual(links, [(4, "ok.md")])

    def test_inline_code_link_ignored(self) -> None:
        _, links = self.scan("行内 `[链接](missing.md)` 不渲染")
        self.assertEqual(links, [])

    def test_single_line_html_comment_ignored(self) -> None:
        _, links = self.scan("<!-- [注释](missing.md) -->\n[真](ok.md)")
        self.assertEqual(links, [(2, "ok.md")])

    def test_multiline_html_comment_ignored(self) -> None:
        text = "<!--\n[注释](missing.md)\n-->\n[真](ok.md)"
        _, links = self.scan(text)
        self.assertEqual(links, [(4, "ok.md")])

    def test_content_after_comment_close_scanned(self) -> None:
        text = "<!--\n-->\n[真](ok.md)"
        _, links = self.scan(text)
        self.assertEqual(links, [(3, "ok.md")])

    def test_bom_tolerated(self) -> None:
        path = self.base_dir / "bom.md"
        path.write_bytes("\ufeff# BOM 标题\n".encode("utf-8"))
        anchors, _ = self.module.scan_markdown(path)
        self.assertEqual(anchors, {"bom-标题"})

    def test_empty_file(self) -> None:
        anchors, links = self.scan("")
        self.assertEqual(anchors, set())
        self.assertEqual(links, [])

    def test_non_utf8_file_skipped_with_warning(self) -> None:
        path = self.base_dir / "gbk.md"
        path.write_bytes("[链接](missing.md) 中文".encode("gbk"))
        output = io.StringIO()
        with redirect_stdout(output):
            anchors, links = self.module.scan_markdown(path)
        self.assertEqual(anchors, set())
        self.assertEqual(links, [])
        self.assertIn("::warning::", output.getvalue())
        self.assertIn("非 UTF-8", output.getvalue())


class EndToEndTest(unittest.TestCase):
    """临时根目录夹具上的模块级全流程测试。"""

    def run_md_check(self, md_files: dict | None = None,
                     raw_files: dict | None = None) -> tuple:
        return load_md_check(md_files, raw_files)

    def test_clean_root_passes(self) -> None:
        module, exit_code, output = self.run_md_check(
            {"README.md": "# 标题\n[链接](#标题)\n"})
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])
        self.assertIn("✅ Markdown 链接检查通过", output)

    def test_stats_output(self) -> None:
        module, exit_code, output = self.run_md_check(
            {"a.md": "# A\n[外](https://example.com/x)\n[绝](/abs/path)\n"})
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.stats["files"], 1)
        self.assertEqual(module.stats["links"], 2)
        self.assertEqual(module.stats["skipped"], 2)
        self.assertIn("1 个文件, 提取 2 条链接(其中 2 条外部/绝对路径跳过)", output)

    def test_broken_relative_link_detected(self) -> None:
        module, exit_code, _ = self.run_md_check({"a.md": "[链接](missing.md)"})
        self.assertEqual(exit_code, 1)
        self.assertIn("a.md:1: 链接目标不存在: missing.md",
                      "\n".join(module.errors))

    def test_same_page_anchor(self) -> None:
        module, exit_code, _ = self.run_md_check(
            {"a.md": "# 标题\n[链接](#标题)\n"})
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])

    def test_same_page_anchor_missing(self) -> None:
        module, exit_code, _ = self.run_md_check(
            {"a.md": "# 标题\n[链接](#不存在)\n"})
        self.assertEqual(exit_code, 1)
        self.assertIn("a.md:2: 锚点不存在: #不存在",
                      "\n".join(module.errors))

    def test_cross_file_anchor(self) -> None:
        files = {"a.md": "[链接](b.md#小节)", "b.md": "## 小节\n正文\n"}
        module, exit_code, _ = self.run_md_check(files)
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])

    def test_cross_file_anchor_missing(self) -> None:
        files = {"a.md": "[链接](b.md#不存在)", "b.md": "## 小节\n"}
        module, exit_code, _ = self.run_md_check(files)
        self.assertEqual(exit_code, 1)
        self.assertIn("a.md:1: 跨文件锚点不存在: b.md#不存在",
                      "\n".join(module.errors))

    def test_anchor_cache_reuse_across_files(self) -> None:
        # 两个文件引用同一目标的同一锚点, 缓存命中不产生错误
        files = {
            "a.md": "[链接](b.md#小节)",
            "c.md": "[链接](b.md#小节)",
            "b.md": "## 小节\n",
        }
        module, exit_code, _ = self.run_md_check(files)
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])

    def test_duplicate_heading_suffix_link(self) -> None:
        module, exit_code, _ = self.run_md_check(
            {"a.md": "## 同名\n## 同名\n[链接](#同名-1)\n"})
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])

    def test_external_and_absolute_links_skipped(self) -> None:
        module, exit_code, _ = self.run_md_check({
            "a.md": "[https](https://example.com/x#frag)\n"
                    "[mailto](mailto:a@b.c)\n"
                    "[绝对](/docs/missing.md)\n",
        })
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])

    def test_wiki_style_link_resolves_md(self) -> None:
        files = {"Page.md": "# P\n", "a.md": "[链接](Page)\n"}
        module, exit_code, _ = self.run_md_check(files)
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])

    def test_wiki_style_link_missing(self) -> None:
        module, exit_code, _ = self.run_md_check({"a.md": "[链接](Missing)\n"})
        self.assertEqual(exit_code, 1)
        self.assertIn("a.md:1: 链接目标不存在: Missing",
                      "\n".join(module.errors))

    def test_percent20_encoded_link(self) -> None:
        files = {"foo bar.md": "# T\n", "a.md": "[链接](foo%20bar.md)\n"}
        module, exit_code, _ = self.run_md_check(files)
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])

    def test_link_target_with_space_rejected(self) -> None:
        module, exit_code, _ = self.run_md_check(
            {"a.md": "[链接](foo bar.md)\n"})
        self.assertEqual(exit_code, 1)
        self.assertIn("a.md:1: 链接目标含空格", "\n".join(module.errors))

    def test_empty_link_target_rejected(self) -> None:
        module, exit_code, _ = self.run_md_check({"a.md": "[链接]()\n"})
        self.assertEqual(exit_code, 1)
        self.assertIn("a.md:1: 空链接目标", "\n".join(module.errors))

    def test_pruned_dirs_not_scanned(self) -> None:
        files = {
            "out/bad.md": "[坏](missing.md)\n",
            "build/bad.md": "[坏](missing.md)\n",
            "node_modules/bad.md": "[坏](missing.md)\n",
            "third_party/googletest/docs/README.md": "[坏](../../docs)\n",
            "good.md": "# 好\n",
        }
        module, exit_code, _ = self.run_md_check(files)
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])
        self.assertEqual(module.stats["files"], 1)

    def test_subdirectory_relative_link(self) -> None:
        files = {"docs/b.md": "# S\n", "a.md": "[链接](docs/b.md)\n"}
        module, exit_code, _ = self.run_md_check(files)
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])

    def test_url_encoded_same_page_anchor(self) -> None:
        module, exit_code, _ = self.run_md_check(
            {"a.md": "# 标题\n[链接](#%E6%A0%87%E9%A2%98)\n"})
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])

    def test_fenced_link_ignored_end_to_end(self) -> None:
        module, exit_code, _ = self.run_md_check(
            {"a.md": "```\n[围栏](missing.md)\n```\n"})
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])

    def test_html_comment_link_ignored_end_to_end(self) -> None:
        module, exit_code, _ = self.run_md_check(
            {"a.md": "<!-- [注释](missing.md) -->\n<!--\n[注释2](missing.md)\n-->\n"})
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])

    def test_inline_code_link_ignored_end_to_end(self) -> None:
        module, exit_code, _ = self.run_md_check(
            {"a.md": "行内 `[链接](missing.md)` 不渲染\n"})
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])

    def test_anchor_tag_link_end_to_end(self) -> None:
        module, exit_code, _ = self.run_md_check(
            {"a.md": '<a id="custom"></a>\n[链接](#custom)\n'})
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])

    def test_non_utf8_md_skipped_end_to_end(self) -> None:
        # 非 UTF-8 文件跳过链接检查(仅告警), 不产生错误也不崩溃
        module, exit_code, output = self.run_md_check(
            raw_files={"bad.md": "[链接](missing.md) 中文".encode("gbk")})
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])
        self.assertIn("::warning::", output)
        self.assertIn("非 UTF-8", output)


if __name__ == "__main__":
    unittest.main()
