#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
单工作流规范校验器（W0-W5）

规范：本仓库 CI 统一收敛为唯一工作流文件（单工作流规范），新增或修改工作流后必须
     通过本校验器（主工作流 tools-test 任务自动执行）。
检查：
    W0 结构错误 —— YAML 无法解析 / 顶层不是映射 / jobs 缺失、非映射或为空 /
                  任务定义不是映射 / needs 不是字符串或字符串列表；
    W1 文件数量 —— .github/workflows 下有且仅有一个 .yml/.yaml 工作流文件（零个或
                  多个均违规；多文件时仅分析文件名排序后的第一个，其余逐个报多余）；
    W2 孤立任务 —— 任务既没有 needs 依赖、也没有其他任务依赖它（单任务工作流豁免）；
    W3 图不连通 —— 任务依赖图存在多个互不相关的连通分量；
    W4 循环依赖 —— 任务之间通过 needs 形成环（含自环）；
    W5 未知依赖 —— needs 引用了 jobs 中不存在的任务。
输出：default 模式逐条输出 GitHub Actions ::error:: 注解（CI 界面可点击定位）；
     --format json 输出 {ok, workflows, violations} 结构供脚本消费。
用法：
    python tools/ci/check-workflow.py                          # 校验仓库 .github/workflows
    python tools/ci/check-workflow.py --format json            # JSON 输出
    python tools/ci/check-workflow.py --workflows-dir <dir>    # 自定义工作流目录
退出码：0 校验通过；1 存在违规；2 运行环境错误（缺少 PyYAML）。
"""

from __future__ import annotations

import argparse
import io
import json
import sys
from collections import deque
from dataclasses import dataclass
from pathlib import Path

try:
    import yaml
    PYYAML_AVAILABLE = True
except ImportError:
    yaml = None  # 缺失时由 main() 拦截并以退出码 2 报环境错误
    PYYAML_AVAILABLE = False

# 路径按脚本位置推导(tools/ci -> 仓库根), 不依赖当前工作目录
ROOT = Path(__file__).resolve().parents[2]
DEFAULT_WORKFLOWS_DIR = ROOT / ".github" / "workflows"
# YAML 解析错误详情折叠上限(保证 ::error:: 注解单行且不超长)
YAML_ERROR_DETAIL_LIMIT = 300


@dataclass(frozen=True)
class Violation:
    """一条规范违规。"""

    rule: str      # W0-W5
    severity: str  # error / warning
    target: str    # 文件级违规为文件名, 任务级违规为 "文件名/任务ID"
    message: str

    def as_dict(self) -> dict[str, str]:
        return {"rule": self.rule, "severity": self.severity,
                "target": self.target, "message": self.message}


@dataclass
class JobNode:
    """工作流任务节点（needs 为排序去重后的依赖名列表，含未知依赖，由 W5 报告）。"""

    job_id: str
    needs: list[str]


class WorkflowGraph:
    """任务依赖图：节点为任务 ID，有向边表示 needs 依赖（孤立/连通检查用无向视角）。"""

    def __init__(self, nodes: dict[str, JobNode]) -> None:
        self.nodes = nodes

    def undirected_neighbors(self) -> dict[str, set[str]]:
        """无向邻接表（忽略指向不存在任务的依赖，该情况由 W5 报告）。"""
        neighbors: dict[str, set[str]] = {job_id: set() for job_id in self.nodes}
        for job_id, node in self.nodes.items():
            for dep in node.needs:
                if dep in self.nodes:
                    neighbors[job_id].add(dep)
                    neighbors[dep].add(job_id)
        return neighbors

    def connected_components(self) -> list[list[str]]:
        """BFS 求连通分量，返回排序后的分量列表（每个分量为排序的任务 ID 列表）。"""
        neighbors = self.undirected_neighbors()
        visited: set[str] = set()
        components: list[list[str]] = []
        for start in sorted(self.nodes):
            if start in visited:
                continue
            visited.add(start)
            queue: deque[str] = deque([start])
            component: list[str] = []
            while queue:
                job_id = queue.popleft()
                component.append(job_id)
                for dep in sorted(neighbors[job_id]):
                    if dep not in visited:
                        visited.add(dep)
                        queue.append(dep)
            components.append(sorted(component))
        return components

    def find_cycle(self) -> list[str] | None:
        """迭代三色 DFS 求环，返回形如 [a, b, a] 的循环路径（自环为 [a, a]），无环返回 None。"""
        white, gray, black = 0, 1, 2
        color = {job_id: white for job_id in self.nodes}
        parent: dict[str, str] = {}
        for start in sorted(self.nodes):
            if color[start] != white:
                continue
            color[start] = gray
            stack: list[tuple[str, int]] = [(start, 0)]
            while stack:
                job_id, index = stack[-1]
                needs = self.nodes[job_id].needs
                if index < len(needs):
                    stack[-1] = (job_id, index + 1)
                    dep = needs[index]
                    if dep not in self.nodes:
                        continue
                    if color[dep] == gray:
                        # 沿父链回溯到灰色节点, 还原循环路径
                        chain = [job_id]
                        cursor = job_id
                        while cursor != dep:
                            cursor = parent[cursor]
                            chain.append(cursor)
                        return [job_id] + chain[::-1]
                    if color[dep] == white:
                        parent[dep] = job_id
                        color[dep] = gray
                        stack.append((dep, 0))
                else:
                    color[job_id] = black
                    stack.pop()
        return None


def find_workflow_files(workflows_dir: Path) -> list[Path]:
    """返回目录下所有顶层 .yml/.yaml 工作流文件，按文件名排序（目录不存在视为零个）。"""
    if not workflows_dir.is_dir():
        return []
    files = [
        path
        for pattern in ("*.yml", "*.yaml")
        for path in workflows_dir.glob(pattern)
        if path.is_file()
    ]
    return sorted(files, key=lambda path: path.name)


def collapse_detail(text: str) -> str:
    """将 YAML 错误详情折叠为单行并截断，保证注解可读且不超长。"""
    one_line = " ".join(text.split())
    if len(one_line) > YAML_ERROR_DETAIL_LIMIT:
        return one_line[: YAML_ERROR_DETAIL_LIMIT - 1] + "…"
    return one_line


def parse_workflow(path: Path) -> tuple[WorkflowGraph | None, list[Violation]]:
    """解析单个工作流文件，返回 (依赖图, 违规列表)；结构错误（W0）时依赖图为 None。"""
    # PyYAML 缺失时应由 main() 以退出码 2 拦截, 不会走到这里
    assert yaml is not None
    target = path.name
    try:
        data = yaml.safe_load(path.read_text(encoding="utf-8"))
    except yaml.YAMLError as error:
        return None, [Violation("W0", "error", target,
                                f"工作流 YAML 解析失败: {collapse_detail(str(error))}")]
    except OSError as error:
        return None, [Violation("W0", "error", target,
                                f"工作流文件读取失败: {collapse_detail(str(error))}")]

    if not isinstance(data, dict):
        kind = "文件为空" if data is None else f"顶层结构必须是映射, 实际为 {type(data).__name__}"
        return None, [Violation("W0", "error", target, f"工作流{kind}")]

    if "jobs" not in data:
        return None, [Violation("W0", "error", target, "工作流缺少 jobs 段")]
    jobs = data["jobs"]
    if not isinstance(jobs, dict):
        return None, [Violation("W0", "error", target,
                                f"jobs 段必须是映射, 实际为 {type(jobs).__name__}")]
    if not jobs:
        return None, [Violation("W0", "error", target, "jobs 段为空: 至少需要一个任务")]

    violations: list[Violation] = []
    nodes: dict[str, JobNode] = {}
    for raw_job_id, raw_body in jobs.items():
        if not isinstance(raw_job_id, str):
            violations.append(Violation("W0", "error", target,
                                        f"任务名必须是字符串: {raw_job_id!r}"))
            continue
        if not isinstance(raw_body, dict):
            violations.append(Violation("W0", "error", f"{target}/{raw_job_id}",
                                        f"任务 {raw_job_id} 的定义必须是映射"))
            continue
        needs_value = raw_body.get("needs")
        if needs_value is None:
            needs: list[str] = []
        elif isinstance(needs_value, str):
            needs = [needs_value]
        elif isinstance(needs_value, list) and all(isinstance(item, str) for item in needs_value):
            needs = list(needs_value)
        else:
            violations.append(Violation("W0", "error", f"{target}/{raw_job_id}",
                                        f"任务 {raw_job_id} 的 needs 必须是字符串或字符串列表"))
            continue
        nodes[raw_job_id] = JobNode(raw_job_id, sorted(set(needs)))

    if not nodes:
        return None, violations
    return WorkflowGraph(nodes), violations


def check_graph(graph: WorkflowGraph, target: str) -> list[Violation]:
    """对依赖图执行 W2（孤立）/ W3（连通）/ W4（环）/ W5（未知依赖）检查。"""
    violations: list[Violation] = []

    # W5: needs 引用了不存在的任务
    for job_id in sorted(graph.nodes):
        for dep in graph.nodes[job_id].needs:
            if dep not in graph.nodes:
                violations.append(Violation(
                    "W5", "error", f"{target}/{job_id}",
                    f"任务 {job_id} 的 needs 引用了不存在的任务: {dep}"))

    # W2: 孤立任务(无任何边), 单任务工作流豁免
    if len(graph.nodes) > 1:
        neighbors = graph.undirected_neighbors()
        for job_id in sorted(graph.nodes):
            if not neighbors[job_id]:
                violations.append(Violation(
                    "W2", "error", f"{target}/{job_id}",
                    f"任务 {job_id} 为孤立任务: 既没有 needs 依赖，也没有其他任务依赖它"))

    # W3: 依赖图必须连通
    components = graph.connected_components()
    if len(components) > 1:
        grouped = "、".join("{" + "、".join(component) + "}" for component in components)
        violations.append(Violation(
            "W3", "error", target,
            f"任务依赖图不连通: 存在 {len(components)} 个互不相关的连通分量: {grouped}，"
            "单工作流要求所有任务通过 needs 相互关联"))

    # W4: needs 依赖成环
    cycle = graph.find_cycle()
    if cycle is not None:
        violations.append(Violation(
            "W4", "error", target, f"任务依赖存在循环: {' -> '.join(cycle)}"))

    return violations


def run_checks(workflows_dir: Path) -> tuple[list[str], list[Violation]]:
    """执行全部检查，返回 (工作流文件名列表, 按 (规则, 目标) 排序后的违规列表)。"""
    files = find_workflow_files(workflows_dir)
    names = [path.name for path in files]
    violations: list[Violation] = []

    # W1: 有且仅有一个工作流文件
    if not files:
        violations.append(Violation(
            "W1", "error", str(workflows_dir),
            f"未找到任何工作流文件: {workflows_dir} 下应有且仅有一个 .yml/.yaml 工作流"))
        return names, violations
    primary, extras = files[0], files[1:]
    for extra in extras:
        violations.append(Violation(
            "W1", "error", extra.name,
            f"系统已存在工作流 {primary.name}，请先删除现有工作流后再新增"
            f"（单工作流规范: 有且仅有一个工作流文件; 多余文件: {extra.name}）"))

    # 仅分析文件名排序后的第一个工作流, 其余按多余文件报告
    graph, parse_violations = parse_workflow(primary)
    violations.extend(parse_violations)
    if graph is not None:
        violations.extend(check_graph(graph, primary.name))

    violations.sort(key=lambda violation: (violation.rule, violation.target))
    return names, violations


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="单工作流规范校验器(W0-W5): 工作流文件有且仅有一个, "
                    "任务依赖图无孤立任务、连通且无环")
    parser.add_argument("--workflows-dir", type=Path, default=DEFAULT_WORKFLOWS_DIR,
                        help="工作流目录(默认: 仓库 .github/workflows)")
    parser.add_argument("--format", choices=("default", "json"), default="default",
                        help="输出格式(default 输出 GitHub Actions 注解; json 供脚本消费)")
    args = parser.parse_args(argv)

    if not PYYAML_AVAILABLE:
        print("::error::缺少依赖 PyYAML: 请先执行 pip install pyyaml 后重试", file=sys.stderr)
        return 2

    names, violations = run_checks(args.workflows_dir)

    if args.format == "json":
        payload = {
            "ok": not violations,
            "workflows": names,
            "violations": [violation.as_dict() for violation in violations],
        }
        print(json.dumps(payload, ensure_ascii=False, indent=2))
    else:
        for name in names:
            print(f"工作流文件: {name}")
        for violation in violations:
            print(f"::error::[{violation.rule}] {violation.target}: {violation.message}")
        if not violations:
            print("单工作流规范校验通过: 有且仅有一个工作流文件, "
                  "任务依赖图连通且无孤立任务、无循环依赖。")

    return 0 if not violations else 1


if __name__ == "__main__":
    # Windows 控制台默认 GBK, 统一切到 UTF-8 避免中文注解乱码/报错
    if isinstance(sys.stdout, io.TextIOWrapper):
        sys.stdout.reconfigure(encoding="utf-8")
    if isinstance(sys.stderr, io.TextIOWrapper):
        sys.stderr.reconfigure(encoding="utf-8")
    sys.exit(main())
