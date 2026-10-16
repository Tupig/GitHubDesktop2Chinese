#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check-workflow.py 的单元测试。

用临时目录构造最小 YAML 夹具，逐一覆盖 W0-W6 规则与 main() 的 JSON/退出码行为；
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
from contextlib import redirect_stderr, redirect_stdout
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
    """W0-W6 规则与 main() 行为测试（每个用例独立临时目录，互不影响）。"""

    def setUp(self) -> None:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.base_dir = Path(tmp.name)
        self.workflows_dir = self.base_dir / "workflows"
        self.workflows_dir.mkdir()

    # -- 夹具辅助 --
    def write_workflow(self, content: str, filename: str = "tupigghd2chinese.yml") -> Path:
        path = self.workflows_dir / filename
        path.write_text(content, encoding="utf-8")
        return path

    def rules_of(self, violations: list) -> set[str]:
        return {violation.rule for violation in violations}

    # -- W1 文件数量 --
    def test_valid_chain_passes(self) -> None:
        self.write_workflow(VALID_CHAIN)
        names, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(names, ["tupigghd2chinese.yml"])
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
        self.assertEqual([v.target for v in w2], ["tupigghd2chinese.yml/c"])

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
        # 分量分组(含分隔格式)是诊断输出本身, 有意断言其内容(#146: 非脆弱文案绑定)
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
        # 环路径是诊断输出本身, 有意断言其内容(#146: 换算法文案即变属预期诊断变更)
        self.assertIn("b -> a -> b", violations[0].message)

    def test_self_cycle_detected(self) -> None:
        self.write_workflow(
            "on: push\n"
            "jobs:\n"
            "  a:\n    needs: a\n    runs-on: ubuntu-24.04\n"
        )
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(self.rules_of(violations), {"W4"})
        # 同上: 自环路径为诊断输出内容
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
        self.assertEqual(violations[0].target, "tupigghd2chinese.yml/b")
        self.assertIn("ghost", violations[0].message)

    # -- W6 needs 表达式引用有效性 --
    def test_w6_indirect_outputs_reference_detected(self) -> None:
        # #152 历史形态回归: c 仅直接依赖 b, 却读取 needs.a.outputs（a 为间接依赖, 恒空）
        self.write_workflow(
            "on: push\n"
            "jobs:\n"
            "  a:\n    runs-on: ubuntu-24.04\n"
            "  b:\n    needs: a\n    runs-on: ubuntu-24.04\n"
            "  c:\n"
            "    needs: b\n"
            "    runs-on: ubuntu-24.04\n"
            "    steps:\n"
            "      - run: echo ${{ needs.a.outputs.version }}\n"
        )
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(self.rules_of(violations), {"W6"})
        violation = violations[0]
        self.assertEqual(violation.target, "tupigghd2chinese.yml/c")
        self.assertIn("非直接依赖", violation.message)
        self.assertIn("间接依赖不传递", violation.message)

    def test_w6_indirect_result_reference_detected(self) -> None:
        self.write_workflow(
            "on: push\n"
            "jobs:\n"
            "  a:\n    runs-on: ubuntu-24.04\n"
            "  b:\n    needs: a\n    runs-on: ubuntu-24.04\n"
            "  c:\n"
            "    needs: b\n"
            "    runs-on: ubuntu-24.04\n"
            "    if: ${{ needs.a.result == 'success' }}\n"
        )
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(self.rules_of(violations), {"W6"})
        self.assertIn("非直接依赖", violations[0].message)

    def test_w6_reference_to_missing_job_detected(self) -> None:
        self.write_workflow(
            "on: push\n"
            "jobs:\n"
            "  a:\n    runs-on: ubuntu-24.04\n"
            "  b:\n    needs: a\n    runs-on: ubuntu-24.04\n"
            "  c:\n"
            "    needs: b\n"
            "    runs-on: ubuntu-24.04\n"
            "    env:\n"
            "      V: ${{ needs.ghost.outputs.ver }}\n"
        )
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(self.rules_of(violations), {"W6"})
        self.assertIn("不存在的任务", violations[0].message)
        self.assertIn("ghost", violations[0].message)

    def test_w6_direct_references_pass(self) -> None:
        self.write_workflow(
            "on: push\n"
            "jobs:\n"
            "  a:\n    runs-on: ubuntu-24.04\n"
            "  b:\n"
            "    needs: [a]\n"
            "    runs-on: ubuntu-24.04\n"
            "    env:\n"
            "      V: ${{ needs.a.outputs.version }}\n"
            "    if: ${{ needs.a.result == 'success' }}\n"
        )
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(violations, [])

    def test_w6_unknown_needs_not_double_reported(self) -> None:
        # ghost 同时出现在 needs 列表与表达式中: 未知依赖仅由 W5 报告一次, W6 不重复
        self.write_workflow(
            "on: push\n"
            "jobs:\n"
            "  a:\n    runs-on: ubuntu-24.04\n"
            "  b:\n"
            "    needs: [a, ghost]\n"
            "    runs-on: ubuntu-24.04\n"
            "    env:\n"
            "      V: ${{ needs.ghost.outputs.ver }}\n"
            "  c:\n    needs: b\n    runs-on: ubuntu-24.04\n"
        )
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(self.rules_of(violations), {"W5"})
        self.assertEqual(len(violations), 1)

    def test_w6_star_result_reference_ignored(self) -> None:
        # needs.* 星形引用为 GitHub 合法语法, 不按未知任务误报
        self.write_workflow(
            "on: push\n"
            "jobs:\n"
            "  a:\n    runs-on: ubuntu-24.04\n"
            "  b:\n"
            "    needs: a\n"
            "    runs-on: ubuntu-24.04\n"
            "    if: ${{ !cancelled() && contains(needs.*.result, 'failure') }}\n"
        )
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(violations, [])

    def test_w6_refs_in_nested_step_scanned(self) -> None:
        # 引用位于 steps[].with/env 嵌套结构中, 递归扫描应命中; 同任务重复引用去重为一条
        self.write_workflow(
            "on: push\n"
            "jobs:\n"
            "  a:\n    runs-on: ubuntu-24.04\n"
            "  b:\n"
            "    needs: a\n"
            "    runs-on: ubuntu-24.04\n"
            "    steps:\n"
            "      - name: check\n"
            "        uses: fake/action@v1\n"
            "        with:\n"
            "          base: ${{ needs.ghost.outputs.ver }}\n"
            "        env:\n"
            "          REF: ${{ needs.ghost.result }}\n"
        )
        _, violations = check_workflow.run_checks(self.workflows_dir)
        w6 = [v for v in violations if v.rule == "W6"]
        self.assertEqual(len(w6), 1)
        self.assertEqual(w6[0].target, "tupigghd2chinese.yml/b")
        self.assertIn("不存在的任务", w6[0].message)

    def test_w6_notify_like_multi_result_pass(self) -> None:
        # CiNotify 形态: 多个直接依赖的 result 引用全部合法
        self.write_workflow(
            "on: push\n"
            "jobs:\n"
            "  a:\n    runs-on: ubuntu-24.04\n"
            "  b:\n    needs: a\n    runs-on: ubuntu-24.04\n"
            "  notify:\n"
            "    needs: [a, b]\n"
            "    runs-on: ubuntu-24.04\n"
            "    if: ${{ needs.a.result != 'success' || needs.b.result != 'success' }}\n"
        )
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(violations, [])

    # -- W0 结构错误 --
    def test_invalid_yaml_reported(self) -> None:
        self.write_workflow("name: 测试\non: [未闭合\n")
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(len(violations), 1)
        violation = violations[0]
        self.assertEqual(violation.rule, "W0")
        self.assertEqual(violation.target, "tupigghd2chinese.yml")
        self.assertIn("解析失败", violation.message)
        # #146: 上限引用被测模块导出常量(前缀 + collapse 截断), 改常量测试自动跟随
        prefix = "工作流 YAML 解析失败: "
        self.assertLessEqual(len(violation.message), len(prefix) + check_workflow.YAML_ERROR_DETAIL_LIMIT)

    def test_non_utf8_workflow_reported(self) -> None:
        # GBK 编码的中文在 UTF-8 解码时抛 UnicodeDecodeError(ValueError 子类, 非 OSError)
        path = self.workflows_dir / "tupigghd2chinese.yml"
        path.write_bytes("name: 测试\non: push\njobs:\n  a:\n    runs-on: ubuntu-24.04\n".encode("gbk"))
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual([v.rule for v in violations], ["W0"])
        self.assertIn("非 UTF-8 编码", violations[0].message)

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
        self.assertEqual(violations[0].target, "tupigghd2chinese.yml/a")
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
        self.assertEqual(violations[0].target, "tupigghd2chinese.yml/b")
        self.assertIn("needs 必须是字符串或字符串列表", violations[0].message)

    # #146: 补覆盖缺口——非字符串 job 名(YAML 数字键被解析为 int)
    def test_non_string_job_name_reported(self) -> None:
        self.write_workflow("on: push\njobs:\n  123:\n    runs-on: ubuntu-24.04\n")
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual([v.rule for v in violations], ["W0"])
        self.assertIn("任务名必须是字符串", violations[0].message)
        self.assertIn("123", violations[0].message)

    # #146: 补覆盖缺口——needs: null 与缺省 needs 同义(空依赖列表), 不应误报
    def test_needs_null_treated_as_empty(self) -> None:
        self.write_workflow(
            "on: push\n"
            "jobs:\n"
            "  a:\n    needs: null\n    runs-on: ubuntu-24.04\n"
            "  b:\n    needs: a\n    runs-on: ubuntu-24.04\n"
        )
        _, violations = check_workflow.run_checks(self.workflows_dir)
        self.assertEqual(violations, [])

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
        self.assertEqual(names, ["tupigghd2chinese.yml"])

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
        self.assertEqual(payload["workflows"], ["tupigghd2chinese.yml"])
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

    # #146: 补覆盖缺口——PyYAML 缺失时 main() 以退出码 2 报环境错误(monkeypatch 可用性标志)
    def test_main_returns_2_without_pyyaml(self) -> None:
        self.write_workflow(VALID_CHAIN)
        original = check_workflow.PYYAML_AVAILABLE
        check_workflow.PYYAML_AVAILABLE = False
        try:
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                exit_code = check_workflow.main(["--workflows-dir", str(self.workflows_dir)])
        finally:
            check_workflow.PYYAML_AVAILABLE = original
        self.assertEqual(exit_code, 2)


if __name__ == "__main__":
    unittest.main()
