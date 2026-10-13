#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check-workflow.py 的单元测试。

用临时目录构造最小 YAML 夹具，逐一覆盖 W0-W5 规则与 main() 的 JSON/退出码行为；
不依赖仓库实际工作流文件，可在任意分支状态运行。

用法：
    python tools/ci/check-workflow-test.py       # 运行全部用例
    python tools/ci/check-workflow-test.py -v    # 逐用例输出
"""

from __future__ import annotations

import importlib.util
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

# 通过文件路径加载被测模块（文件名含连字符，无法常规 import）
MODULE_PATH = Path(__file__).resolve().parent / "check-workflow.py"
SPEC = importlib.util.spec_from_file_location("check_workflow", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"无法加载被测模块: {MODULE_PATH}")
check_workflow = importlib.util.module_from_spec(SPEC)
sys.modules["check_workflow"] = check_workflow
SPEC.loader.exec_module(check_workflow)

VALID_CHAIN = """\
name: 测试工作流
on:
  push:
jobs:
  a:
    runs-on: ubuntu-24.04
  b:
    needs: a
    runs-on: ubuntu-24.04
  c:
    needs: [a, b]
    runs-on: ubuntu-24.04
"""


class CheckWorkflowTest(unittest.TestCase):
    """W0-W5 规则与 main() 行为测试（每个用例独立临时目录，互不影响）。"""

    def setUp(self) -> None:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.base_dir = Path(tmp.name)
        self.workflows_dir = self.base_dir / "workflows"
        self.workflows_dir.mkdir()

    # -- 夹具辅助 --
    def write_workflow(self, content: str, filename: str = "ghdesktop2chinese.yml") -> Path:
        path = self.workflows_dir / filename
        path.write_text(content, encoding="utf-8")
        return path

    def rules_of(self, violations: list) -> set[str]:
        return {violation.rule for violation in violations}

    # -- W1 文件数量 --
    def test_valid_chain_passes(self) -> None:
        self.write_workflow(VALID_CHAIN)
        names, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(names, ["ghdesktop2chinese.yml"])
        self.assertEqual(violations, [])

    def test_duplicate_workflow_rejected(self) -> None:
        self.write_workflow(VALID_CHAIN, filename="a.yml")
        self.write_workflow(VALID_CHAIN, filename="b.yml")
        names, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(names, ["a.yml", "b.yml"])
        # 仅分析排序第一个(a.yml, 且其本身合法), b.yml 按多余文件报 W1
        self.assertEqual(len(violations), 1)
        violation = violations[0]
        self.assertEqual(violation.rule, "W1")
        self.assertEqual(violation.target, "b.yml")
        self.assertIn("系统已存在工作流 a.yml", violation.message)
        self.assertIn("请先删除现有工作流", violation.message)

    def test_zero_workflows(self) -> None:
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual([v.rule for v in violations], ["W1"])
        self.assertIn("未找到任何工作流文件", violations[0].message)

    def test_missing_workflows_dir(self) -> None:
        _, violations = check_workflow.run_checks(self.base_dir / "不存在")
        self.assertEqual([v.rule for v in violations], ["W1"])
        self.assertIn("未找到任何工作流文件", violations[0].message)

    # -- W2/W3 孤立与连通 --
    def test_orphan_job_detected(self) -> None:
        self.write_workflow(
            "on: push\n"
            "jobs:\n"
            "  a:\n    runs-on: ubuntu-24.04\n"
            "  b:\n    needs: a\n    runs-on: ubuntu-24.04\n"
            "  c:\n    runs-on: ubuntu-24.04\n"
        )
        _, violations = check_workflow.run_checks(self.workflows_dir)
        rules = self.rules_of(violations)
        self.assertIn("W2", rules)
        self.assertIn("W3", rules)
        w2 = [v for v in violations if v.rule == "W2"]
        self.assertEqual([v.target for v in w2], ["ghdesktop2chinese.yml/c"])

    def test_disconnected_components_without_orphans(self) -> None:
        self.write_workflow(
            "on: push\n"
            "jobs:\n"
            "  a:\n    runs-on: ubuntu-24.04\n"
            "  b:\n    needs: a\n    runs-on: ubuntu-24.04\n"
            "  c:\n    runs-on: ubuntu-24.04\n"
            "  d:\n    needs: c\n    runs-on: ubuntu-24.04\n"
        )
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(self.rules_of(violations), {"W3"})
        self.assertIn("{a、b}", violations[0].message)
        self.assertIn("{c、d}", violations[0].message)

    # -- W4 循环依赖 --
    def test_cycle_detected(self) -> None:
        self.write_workflow(
            "on: push\n"
            "jobs:\n"
            "  a:\n    needs: b\n    runs-on: ubuntu-24.04\n"
            "  b:\n    needs: a\n    runs-on: ubuntu-24.04\n"
        )
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(self.rules_of(violations), {"W4"})
        self.assertIn("b -> a -> b", violations[0].message)

    def test_self_cycle_detected(self) -> None:
        self.write_workflow(
            "on: push\n"
            "jobs:\n"
            "  a:\n    needs: a\n    runs-on: ubuntu-24.04\n"
        )
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(self.rules_of(violations), {"W4"})
        self.assertIn("a -> a", violations[0].message)

    # -- W5 未知依赖 --
    def test_unknown_needs_detected(self) -> None:
        self.write_workflow(
            "on: push\n"
            "jobs:\n"
            "  a:\n    runs-on: ubuntu-24.04\n"
            "  b:\n    needs: [a, ghost]\n    runs-on: ubuntu-24.04\n"
            "  c:\n    needs: b\n    runs-on: ubuntu-24.04\n"
        )
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(self.rules_of(violations), {"W5"})
        self.assertEqual(violations[0].target, "ghdesktop2chinese.yml/b")
        self.assertIn("ghost", violations[0].message)

    # -- W0 结构错误 --
    def test_invalid_yaml_reported(self) -> None:
        self.write_workflow("name: 测试\non: [未闭合\n")
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(len(violations), 1)
        violation = violations[0]
        self.assertEqual(violation.rule, "W0")
        self.assertEqual(violation.target, "ghdesktop2chinese.yml")
        self.assertIn("解析失败", violation.message)
        self.assertLessEqual(len(violation.message), 400)

    def test_empty_workflow_file(self) -> None:
        self.write_workflow("")
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual([v.rule for v in violations], ["W0"])
        self.assertIn("为空", violations[0].message)

    def test_missing_jobs_section(self) -> None:
        self.write_workflow("name: 测试\non: push\n")
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual([v.rule for v in violations], ["W0"])
        self.assertIn("缺少 jobs 段", violations[0].message)

    def test_empty_jobs_section(self) -> None:
        self.write_workflow("name: 测试\non: push\njobs: {}\n")
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual([v.rule for v in violations], ["W0"])
        self.assertIn("为空", violations[0].message)

    def test_jobs_section_not_mapping(self) -> None:
        self.write_workflow("name: 测试\non: push\njobs:\n  - a\n")
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual([v.rule for v in violations], ["W0"])
        self.assertIn("必须是映射", violations[0].message)

    def test_job_body_not_mapping(self) -> None:
        self.write_workflow("on: push\njobs:\n  a: 123\n")
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual([v.rule for v in violations], ["W0"])
        self.assertEqual(violations[0].target, "ghdesktop2chinese.yml/a")
        self.assertIn("定义必须是映射", violations[0].message)

    def test_needs_with_non_string_element(self) -> None:
        self.write_workflow(
            "on: push\n"
            "jobs:\n"
            "  a:\n    runs-on: ubuntu-24.04\n"
            "  b:\n    needs: [a, 123]\n    runs-on: ubuntu-24.04\n"
        )
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual([v.rule for v in violations], ["W0"])
        self.assertEqual(violations[0].target, "ghdesktop2chinese.yml/b")
        self.assertIn("needs 必须是字符串或字符串列表", violations[0].message)

    # -- 合法形态 --
    def test_needs_variants_accepted(self) -> None:
        self.write_workflow(
            "on: push\n"
            "jobs:\n"
            "  a:\n    runs-on: ubuntu-24.04\n"
            "  b:\n    needs: a\n    runs-on: ubuntu-24.04\n"
            "  c:\n    needs: [a, b]\n    runs-on: ubuntu-24.04\n"
        )
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(violations, [])

    def test_single_job_ok(self) -> None:
        self.write_workflow("on: push\njobs:\n  solo:\n    runs-on: ubuntu-24.04\n")
        names, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(violations, [])
        self.assertEqual(names, ["ghdesktop2chinese.yml"])

    # -- main() 行为 --
    def test_main_json_format(self) -> None:
        self.write_workflow(VALID_CHAIN)
        stdout = io.StringIO()
        with redirect_stdout(stdout):
            exit_code = check_workflow.main(
                ["--workflows-dir", str(self.workflows_dir), "--format", "json"])
        self.assertEqual(exit_code, 0)
        payload = json.loads(stdout.getvalue())
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["workflows"], ["ghdesktop2chinese.yml"])
        self.assertEqual(payload["violations"], [])

    def test_main_exit_code_and_annotation(self) -> None:
        self.write_workflow(
            "on: push\n"
            "jobs:\n"
            "  a:\n    runs-on: ubuntu-24.04\n"
            "  b:\n    needs: a\n    runs-on: ubuntu-24.04\n"
            "  c:\n    runs-on: ubuntu-24.04\n"
        )
        stdout = io.StringIO()
        with redirect_stdout(stdout):
            exit_code = check_workflow.main(["--workflows-dir", str(self.workflows_dir)])
        self.assertEqual(exit_code, 1)
        output = stdout.getvalue()
        self.assertIn("::error::", output)
        self.assertIn("[W2]", output)


if __name__ == "__main__":
    unittest.main()
