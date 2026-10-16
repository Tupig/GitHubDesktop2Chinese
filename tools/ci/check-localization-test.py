#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check-localization.py 的单元测试。

被测脚本按 __file__ 相对路径推导 json/localization.json 与黑名单位置，
且模块级立即执行校验流程——因此将脚本副本放入临时目录夹具（同结构放置
fixture 数据）后经 importlib 执行，即可在不修改校验器本体的前提下，
让模块级全流程（含 sys.exit 退出码与统计输出）完整跑在夹具数据上。

覆盖：合法数据通过 / 节点缺失与类型错误 / 草稿占位符 / 长度与元素边界 /
空 pattern 跳过 / 非法正则 / std::regex 黑名单六规则 / ReDoS 启发式 /
重复项告警 / 问句风格告警 / select 结构三字段与 replace 数组同等校验 /
文件缺失与损坏的退出码 2 / 黑名单缺失降级。

用法：
    python tools/ci/check-localization-test.py       # 运行全部用例
    python tools/ci/check-localization-test.py -v    # 逐用例输出
"""

from __future__ import annotations

import importlib.util
import io
import json
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

SCRIPT_PATH = Path(__file__).resolve().parent / "check-localization.py"

# 校验器支持的仓库根覆盖环境变量（#263）：测试据此把夹具根设为扫描根，
# 从而可直接加载真实脚本（不再复制副本），覆盖率可归因到本源文件。
ROOT_ENV = "GHD2C_CHECK_ROOT"

# 最小合法夹具（version/minversion + 四个映射数组 + 空 select）
BASE_DATA = {
    "version": "3.4.4",
    "minversion": "3.0.0",
    "main": [["Find", "查找"]],
    "renderer": [["Pull", "拉取"]],
    "main_dev": [],
    "renderer_dev": [],
    "select": [],
}

# 黑名单夹具：**直接加载真实规则文件**（tools/auto-maintain/regex-blacklist.json）作为
# 单一事实来源（#271）。此前为「同构独立副本 + 注释称避免漂移」，实际已漂移到与生产
# 不同的规则集/启发式（掩盖了 C++ 与 CI 的口径分歧）。需要特定规则的用例请显式传 blacklist=。
BLACKLIST_PATH = SCRIPT_PATH.parents[1] / "auto-maintain" / "regex-blacklist.json"
BLACKLIST_DATA = json.loads(BLACKLIST_PATH.read_text(encoding="utf-8"))

_module_counter = 0


def run_validator(data: dict | None = None, *, json_text: str | None = None,
                  blacklist: dict | None = None, omit_json: bool = False,
                  omit_blacklist: bool = False, extra_files: dict | None = None) -> tuple:
    """构造临时目录夹具并加载执行**真实**校验器脚本，返回 (module, exit_code, output)。

    exit_code 为 0 表示模块正常执行完毕；sys.exit(N) 被捕获为 N。
    """
    global _module_counter
    _module_counter += 1
    tmp = tempfile.TemporaryDirectory()
    root = Path(tmp.name)
    (root / "tools" / "ci").mkdir(parents=True)
    (root / "tools" / "auto-maintain").mkdir(parents=True)
    (root / "json").mkdir()
    if not omit_json:
        text = json_text if json_text is not None else json.dumps(
            BASE_DATA if data is None else data, ensure_ascii=False)
        (root / "json" / "localization.json").write_text(text, encoding="utf-8")
    if not omit_blacklist:
        (root / "tools" / "auto-maintain" / "regex-blacklist.json").write_text(
            json.dumps(blacklist if blacklist is not None else BLACKLIST_DATA,
                       ensure_ascii=False), encoding="utf-8")
    for rel, text in (extra_files or {}).items():
        target = root / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")
    spec = importlib.util.spec_from_file_location(
        f"check_localization_fixture_{_module_counter}",
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


class CheckLocalizationTest(unittest.TestCase):
    """夹具数据驱动的全流程测试。"""

    def run_validator(self, **kwargs) -> tuple:
        return run_validator(**kwargs)

    def errors_text(self, module) -> str:
        return "\n".join(module.errors)

    def warnings_text(self, module) -> str:
        return "\n".join(module.warnings)

    # -- 正常通过 --
    def test_valid_data_passes(self) -> None:
        module, exit_code, output = self.run_validator()
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])
        self.assertEqual(module.warnings, [])
        self.assertIn("✅ localization.json 质量校验通过", output)
        self.assertIn("映射项统计: main=1 renderer=1", output)

    def test_three_element_entry_accepted(self) -> None:
        data = dict(BASE_DATA)
        data["main"] = [["Find", "查找", "额外说明"]]
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])

    # -- 节点缺失与类型 --
    def test_missing_scalar_nodes(self) -> None:
        data = {k: v for k, v in BASE_DATA.items() if k not in ("version", "minversion")}
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 1)
        joined = self.errors_text(module)
        self.assertIn("缺少节点: version", joined)
        self.assertIn("缺少节点: minversion", joined)

    def test_missing_array_nodes(self) -> None:
        data = {k: v for k, v in BASE_DATA.items()
                if k not in ("main", "renderer", "main_dev", "renderer_dev")}
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 1)
        joined = self.errors_text(module)
        for key in ("main", "renderer", "main_dev", "renderer_dev"):
            self.assertIn(f"缺少节点: {key}", joined)

    def test_array_node_not_list(self) -> None:
        data = dict(BASE_DATA)
        data["main"] = {"Find": "查找"}
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 1)
        self.assertIn("main 应为数组, 实际为 dict", self.errors_text(module))

    # -- 草稿占位符 --
    def test_draft_placeholder_rejected(self) -> None:
        data = dict(BASE_DATA)
        data["main"] = [["Find", "【待翻译】"]]
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 1)
        self.assertIn("存在未替换的草稿占位符【待翻译】", self.errors_text(module))

    # -- 条目结构边界 --
    def test_entry_not_array(self) -> None:
        data = dict(BASE_DATA)
        data["main"] = ["Find"]
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 1)
        self.assertIn("main[0] 不是数组", self.errors_text(module))

    def test_entry_length_out_of_range(self) -> None:
        data = dict(BASE_DATA)
        data["main"] = [["Find"], ["A", "B", "C", "D"]]
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 1)
        joined = self.errors_text(module)
        self.assertIn("main[0] 长度应为 2 或 3，实际 1", joined)
        self.assertIn("main[1] 长度应为 2 或 3，实际 4", joined)

    def test_entry_non_string_element(self) -> None:
        data = dict(BASE_DATA)
        data["main"] = [[123, "查找"]]
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 1)
        self.assertIn("main[0] 含非字符串元素", self.errors_text(module))

    def test_empty_pattern_entries_skipped(self) -> None:
        # 空串与 "" 占位(应用侧视为跳过)不参与任何校验
        data = dict(BASE_DATA)
        data["main"] = [["", "空模式"], ['""', "占位模式"], ["Find", "查找"]]
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])
        self.assertEqual(module.warnings, [])

    # -- 非法正则 --
    def test_invalid_regex_detected(self) -> None:
        data = dict(BASE_DATA)
        data["main"] = [["(", "括号"]]
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 1)
        self.assertIn("main[0] 非法正则", self.errors_text(module))

    # -- std::regex 不兼容黑名单（表驱动覆盖六条规则） --
    def test_blacklist_rules_detected(self) -> None:
        table = [
            ("abc(?<=x)y", "后行断言"),
            ("abc(?<!x)y", "负后行断言"),
            ("(?<name>x)", "命名分组"),
            ("(?P<name>x)", "Python 命名分组"),
            ("\\p{Han}+", "Unicode 属性转义"),
            ("(?i:abc)", "内联 flag"),
        ]
        for pattern, why in table:
            with self.subTest(pattern=pattern):
                data = dict(BASE_DATA)
                data["main"] = [[pattern, "替换"]]
                module, exit_code, _ = self.run_validator(data=data)
                self.assertEqual(exit_code, 1)
                joined = self.errors_text(module)
                self.assertIn("std::regex 不兼容语法", joined)
                self.assertIn(why, joined)

    # -- ReDoS 启发式 --
    def test_redos_heuristic_detected(self) -> None:
        for pattern in ("(a+)+", "(.*x)*"):
            with self.subTest(pattern=pattern):
                data = dict(BASE_DATA)
                data["main"] = [[pattern, "替换"]]
                module, exit_code, _ = self.run_validator(data=data)
                self.assertEqual(exit_code, 1)
                self.assertIn("疑似灾难性回溯(嵌套量词)", self.errors_text(module))

    def test_non_nested_quantifier_not_flagged(self) -> None:
        # 普通量词不触发 ReDoS 启发式
        data = dict(BASE_DATA)
        data["main"] = [["a+b*c?", "替换"]]
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])

    # -- 重复项告警（不阻断） --
    def test_duplicate_pattern_warns(self) -> None:
        data = dict(BASE_DATA)
        data["main"] = [["Find", "查找"], ["Find", "再次查找"]]
        module, exit_code, output = self.run_validator(data=data)
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])
        self.assertIn("main 重复查找项: 'Find' (index 0 与 1)",
                      self.warnings_text(module))
        self.assertIn("1 个警告", output)

    # -- 译文问句风格（表驱动） --
    def test_question_style_table(self) -> None:
        table = [
            (["Status?", "状态?"], True),      # 汉字 + 半角 ? -> 告警
            (["Status?", "状态?\""], True),    # 尾部引号剥离后仍命中
            (["Status?", "AB?"], False),       # ? 前非汉字
            (["Q", "什么? 好的"], False),      # 不以 ? 收尾
            (["Q", "什么？"], False),          # 全角 ？不受影响
            (["Q", "状态 ?"], False),          # ? 前是空格非汉字
        ]
        for entry, should_warn in table:
            with self.subTest(translation=entry[1]):
                data = dict(BASE_DATA)
                data["main"] = [entry]
                module, exit_code, _ = self.run_validator(data=data)
                self.assertEqual(exit_code, 0)
                warned = any("建议改全角？" in w for w in module.warnings)
                self.assertEqual(warned, should_warn)

    # -- select 结构校验 --
    def test_select_missing_fields(self) -> None:
        data = dict(BASE_DATA)
        data["select"] = [{}]
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 1)
        joined = self.errors_text(module)
        self.assertIn("select[0] 缺少 replaceFile", joined)
        self.assertIn("select[0] 缺少 tooltip", joined)
        self.assertIn("select[0] 缺少 enable", joined)
        self.assertIn("select[0] replace 应为数组", joined)

    def test_select_field_type_mismatches(self) -> None:
        data = dict(BASE_DATA)
        data["select"] = [{"replaceFile": 1, "tooltip": 2, "enable": "yes",
                           "replace": "x"}]
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 1)
        joined = self.errors_text(module)
        self.assertIn("select[0] replaceFile 应为字符串", joined)
        self.assertIn("select[0] tooltip 应为字符串", joined)
        self.assertIn("select[0] enable 应为布尔", joined)
        self.assertIn("select[0] replace 应为数组", joined)

    def test_select_replace_file_invalid_value(self) -> None:
        data = dict(BASE_DATA)
        data["select"] = [{"replaceFile": "app.js", "tooltip": "说明",
                           "enable": True, "replace": []}]
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 1)
        self.assertIn("select[0] replaceFile 取值非法: 'app.js'",
                      self.errors_text(module))

    def test_select_full_valid(self) -> None:
        data = dict(BASE_DATA)
        data["select"] = [{
            "replaceFile": "main.js", "tooltip": "说明", "enable": True,
            "replace": [["Find", "查找"]],
        }]
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])

    def test_select_replace_row_invalid(self) -> None:
        data = dict(BASE_DATA)
        data["select"] = [{"replaceFile": "main.js", "tooltip": "说明",
                           "enable": True,
                           "replace": [["Find"], [1, 2], "x"]}]
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 1)
        joined = self.errors_text(module)
        self.assertEqual(joined.count("应为至少 2 个字符串的数组"), 3)

    def test_select_replace_pattern_checks(self) -> None:
        # select[].replace 的正则与主映射数组同等校验
        data = dict(BASE_DATA)
        data["select"] = [{"replaceFile": "main.js", "tooltip": "说明",
                           "enable": True,
                           "replace": [["(", "非法"], ["(?<=a)b", "黑名单"],
                                       ["(a+)+", "嵌套"], ["", "空跳过"]]}]
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 1)
        joined = self.errors_text(module)
        self.assertIn("select[0].replace[0] 非法正则", joined)
        self.assertIn("select[0].replace[1] std::regex 不兼容语法", joined)
        self.assertIn("select[0].replace[2] 疑似灾难性回溯(嵌套量词)", joined)

    def test_select_not_array(self) -> None:
        data = dict(BASE_DATA)
        data["select"] = {"a": 1}
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 1)
        self.assertIn("select 应为数组, 实际为 dict", self.errors_text(module))

    def test_select_entry_not_dict(self) -> None:
        data = dict(BASE_DATA)
        data["select"] = ["x"]
        module, exit_code, _ = self.run_validator(data=data)
        self.assertEqual(exit_code, 1)
        self.assertIn("select[0] 应为对象", self.errors_text(module))

    # -- 加载失败路径 --
    def test_missing_localization_json_exits_2(self) -> None:
        _, exit_code, output = self.run_validator(omit_json=True)
        self.assertEqual(exit_code, 2)
        self.assertIn("::error::无法读取 localization.json", output)

    def test_corrupt_localization_json_exits_2(self) -> None:
        _, exit_code, output = self.run_validator(json_text="{不是合法JSON")
        self.assertEqual(exit_code, 2)
        self.assertIn("::error::无法读取 localization.json", output)

    def test_missing_blacklist_reports_error(self) -> None:
        module, exit_code, _ = self.run_validator(omit_blacklist=True)
        self.assertEqual(exit_code, 1)
        self.assertIn("无法加载黑名单规则", self.errors_text(module))

    def test_missing_blacklist_disables_pattern_rules(self) -> None:
        # 黑名单缺失时 pattern 校验整体降级：不兼容语法/ReDoS 均不再检出
        # (两个 pattern 在 Python re 下均合法, 也不产生非法正则错误)
        data = dict(BASE_DATA)
        data["main"] = [["abc(?<=x)y", "后行"], ["(a+)+", "嵌套"]]
        module, exit_code, _ = self.run_validator(data=data, omit_blacklist=True)
        self.assertEqual(exit_code, 1)
        joined = self.errors_text(module)
        self.assertIn("无法加载黑名单规则", joined)
        self.assertNotIn("std::regex 不兼容语法", joined)
        self.assertNotIn("灾难性回溯", joined)
        self.assertNotIn("非法正则", joined)

    # -- 输出与错误汇总 --
    def test_error_summary_output(self) -> None:
        data = dict(BASE_DATA)
        data["main"] = [["(", "非法"]]
        module, exit_code, output = self.run_validator(data=data)
        self.assertEqual(exit_code, 1)
        self.assertIn("❌ 1 个错误", output)
        self.assertIn("- main[0] 非法正则", output)

    # -- ReDoS 规则跨语言一致性（#271） --
    def test_redos_rule_mirror_match_passes(self) -> None:
        # 头文件镜像与 regex-blacklist.json 逐字一致 -> 无一致性错误
        rule = BLACKLIST_DATA["redos_heuristic"]["regex"]
        header = 'inline constexpr const char kRedosHeuristicRegex[] =\n    R"(' + rule + ')";\n'
        module, exit_code, _ = self.run_validator(
            extra_files={"src/utils/l10n_rules.hpp": header})
        self.assertEqual(exit_code, 0)
        self.assertEqual(module.errors, [])

    def test_redos_rule_mirror_mismatch_rejected(self) -> None:
        # 头文件镜像被单独改动（CI 与 C++ 口径分歧）-> 必须报错阻断
        header = 'inline constexpr const char kRedosHeuristicRegex[] =\n    R"(\\(.*[+*].*\\)\\s*[+*{])";\n'
        module, exit_code, output = self.run_validator(
            extra_files={"src/utils/l10n_rules.hpp": header})
        self.assertEqual(exit_code, 1)
        self.assertIn("ReDoS 规则跨语言不一致", output)

    def test_redos_rule_mirror_missing_constant_rejected(self) -> None:
        # 头文件存在但常量被改名/删除 -> 一致性校验失败（防「改名绕过校验」）
        module, exit_code, output = self.run_validator(
            extra_files={"src/utils/l10n_rules.hpp": "// no constant here\n"})
        self.assertEqual(exit_code, 1)
        self.assertIn("无法在", output)


if __name__ == "__main__":
    unittest.main()
