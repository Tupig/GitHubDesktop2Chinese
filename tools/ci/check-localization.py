# localization.json 数据质量校验(工作流 json-quality job 调用; 自 YAML 外置, 行为不变)
# 检查项: 草稿占位符/节点结构/正则合法性/std::regex 不兼容黑名单/ReDoS 启发式/译文问句风格
import json, re, sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

path = "json/localization.json"
raw = open(path, encoding="utf-8").read()
data = json.loads(raw)

errors = []
warnings = []

if "【待翻译】" in raw:
    errors.append("存在未替换的草稿占位符【待翻译】，请补全译文后再提交")

for key in ["version", "minversion"]:
    if key not in data:
        errors.append(f"缺少节点: {key}")

for key in ["main", "renderer", "main_dev", "renderer_dev"]:
    if key not in data:
        errors.append(f"缺少节点: {key}")
    elif not isinstance(data[key], list):
        errors.append(f"{key} 应为数组, 实际为 {type(data[key]).__name__}")

# std::regex 不兼容语法黑名单: 与 tools/auto-maintain 共用单一事实来源
# 路径按脚本位置推导(tools/ci -> 仓库根), 不依赖当前工作目录
BLACKLIST_PATH = Path(__file__).resolve().parents[2] / "tools" / "auto-maintain" / "regex-blacklist.json"
try:
    with open(BLACKLIST_PATH, encoding="utf-8") as f:
        blacklist = [(r["regex"], r["why"]) for r in json.load(f)["rules"]]
except (OSError, ValueError, KeyError, TypeError) as e:
    errors.append(f"无法加载黑名单规则 {BLACKLIST_PATH}: {e}")
    blacklist = []

def check_array(name, arr):
    seen = {}
    for i, item in enumerate(arr):
        if not isinstance(item, list):
            errors.append(f"{name}[{i}] 不是数组")
            continue
        if len(item) < 2 or len(item) > 3:
            errors.append(f"{name}[{i}] 长度应为 2 或 3，实际 {len(item)}")
            continue
        if not all(isinstance(x, str) for x in item):
            errors.append(f"{name}[{i}] 含非字符串元素")
            continue
        pat = item[0]
        if not pat or pat == '""':
            continue  # 空串与 "" 占位(应用侧视为跳过)不参与校验
        for rx, why in blacklist:
            if re.search(rx, pat):
                errors.append(f"{name}[{i}] std::regex 不兼容语法({why}): {pat!r}")
        try:
            re.compile(pat)
        except re.error as e:
            errors.append(f"{name}[{i}] 非法正则: {pat!r} -> {e}")
        if pat in seen:
            warnings.append(f"{name} 重复查找项: {pat!r} (index {seen[pat]} 与 {i})")
        else:
            seen[pat] = i
        # ReDoS 启发式(仅告警不阻断): 嵌套量词(如 (a+)+ 、 (.*x)* )在长文本上可能灾难性回溯卡死加载器
        if re.search(r"\([^()]*[+*][^()]*\)\s*[+*{]", pat):
            warnings.append(f"{name}[{i}] 疑似灾难性回溯(嵌套量词), 请改写该正则: {pat!r}")
        # 译文问句风格: 去掉尾部引号后以 半角? 收尾且 ? 前为汉字 → 建议全角？
        # (JS 三元/正则语法的 ? 不满足"以 ? 收尾"或 ? 前非汉字, 不会误报)
        t = item[1].rstrip("\"'")
        if t.endswith("?") and re.search(r"[一-鿿]\?$", t):
            warnings.append(f"{name}[{i}] 译文以半角 ? 结尾, 建议改全角？: {item[1]!r}")

for key in ["main", "renderer", "main_dev", "renderer_dev"]:
    if isinstance(data.get(key), list):
        check_array(key, data[key])

selects = data.get("select", [])
if not isinstance(selects, list):
    errors.append(f"select 应为数组, 实际为 {type(selects).__name__}")
else:
    for si, sel in enumerate(selects):
        if not isinstance(sel, dict):
            errors.append(f"select[{si}] 应为对象")
            continue
        if "replaceFile" not in sel:
            errors.append(f"select[{si}] 缺少 replaceFile")
        if "tooltip" not in sel:
            errors.append(f"select[{si}] 缺少 tooltip")
        if "enable" not in sel:
            errors.append(f"select[{si}] 缺少 enable")
        if "replaceFile" in sel and not isinstance(sel["replaceFile"], str):
            errors.append(f"select[{si}] replaceFile 应为字符串")
        if "tooltip" in sel and not isinstance(sel["tooltip"], str):
            errors.append(f"select[{si}] tooltip 应为字符串")
        if "enable" in sel and not isinstance(sel["enable"], bool):
            errors.append(f"select[{si}] enable 应为布尔")
        # 应用侧按字面量精确匹配这两个取值, 其它值整条 select 不生效(静默跳过)
        if isinstance(sel.get("replaceFile"), str) and sel["replaceFile"] not in ("main.js", "renderer.js"):
            errors.append(f"select[{si}] replaceFile 取值非法: {sel['replaceFile']!r}（仅允许 'main.js' / 'renderer.js'）")
        if not isinstance(sel.get("replace"), list):
            errors.append(f"select[{si}] replace 应为数组")

print(f"映射项统计: main={len(data.get('main', []))} renderer={len(data.get('renderer', []))}")
if warnings:
    print(f"\n⚠️  {len(warnings)} 个警告:")
    for w in warnings[:20]:
        print(f"  - {w}")
if errors:
    print(f"\n❌ {len(errors)} 个错误:")
    for e in errors[:30]:
        print(f"  - {e}")
    sys.exit(1)
print("\n✅ localization.json 质量校验通过")
