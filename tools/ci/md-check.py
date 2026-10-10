# Markdown 链接检查(工作流 tools-test job 调用; 自 YAML 外置)
# 检查项: 相对链接目标存在性 / 同页与跨文件锚点存在性(GitHub 风格标题 slug)
# 跳过项: http(s)/mailto 等带 scheme 的外部链接 / 以 / 开头的站内绝对路径
#          / 代码围栏与行内代码内的链接(不参与渲染)
# 约定:   无扩展名目标按 GitHub Wiki 页引用处理(同目录 <名>.md 存在即有效)
import re
import sys
from pathlib import Path
from urllib.parse import unquote

sys.stdout.reconfigure(encoding="utf-8")

# 路径按脚本位置推导(tools/ci -> 仓库根), 不依赖当前工作目录
ROOT = Path(__file__).resolve().parents[2]
# 构建产物/依赖缓存目录不参与扫描
PRUNE_DIRS = {".git", "out", "build", "dist", "node_modules"}

SCHEME_RE = re.compile(r"^[a-zA-Z][a-zA-Z0-9+.\-]*:")
HEADING_RE = re.compile(r"^ {0,3}(#{1,6})\s+(.*?)\s*$")
HEADING_CLOSE_RE = re.compile(r"\s*#+\s*$")
ANCHOR_TAG_RE = re.compile(r"<a\s+(?:id|name)\s*=\s*[\"']([^\"']+)[\"']", re.IGNORECASE)
INLINE_CODE_RE = re.compile(r"`[^`]*`")
LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]*)\)")

errors = []
stats = {"files": 0, "links": 0, "skipped": 0}


def slugify(text):
    """GitHub 风格标题锚点: 小写 -> 去掉非[字母数字_空白-]字符 -> 空格逐个转连字符(不折叠)

    emoji 标题(如 '## 🧪 CI / 自动维护')去掉 emoji 后保留其后空格,
    产物带前导连字符('-ci--自动维护'), 与 GitHub 实际锚点一致。
    """
    text = text.lower()
    text = re.sub(r"[^\w\s-]", "", text, flags=re.UNICODE)
    return text.replace(" ", "-")


def add_anchor(anchors, anchor):
    """登记锚点; 同名标题按 GitHub 规则追加 -1/-2 后缀"""
    if not anchor:
        return
    if anchor not in anchors:
        anchors.add(anchor)
        return
    i = 1
    while f"{anchor}-{i}" in anchors:
        i += 1
    anchors.add(f"{anchor}-{i}")


def scan_markdown(path):
    """解析 markdown: 返回 (锚点集合, [(行号, 链接目标), ...])"""
    anchors = set()
    links = []
    fence = None        # 当前代码围栏标记('```' / '~~~'), None 表示围栏外
    in_comment = False  # HTML 注释跨行状态(注释内链接/标题不渲染)
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    for lineno, line in enumerate(lines, 1):
        stripped = line.lstrip()
        # 1) 围栏内: 只识别闭合标记(围栏优先于 HTML 注释)
        if fence:
            if stripped.startswith(fence):
                fence = None
            continue
        # 2) 跨行 HTML 注释: 结束标记前的内容全部忽略
        if in_comment:
            end = line.find("-->")
            if end == -1:
                continue
            line = line[end + 3:]
            in_comment = False
        # 3) 剥离单行/开启跨行的 HTML 注释
        while True:
            start = line.find("<!--")
            if start == -1:
                break
            end = line.find("-->", start + 4)
            if end == -1:
                line = line[:start]
                in_comment = True
                break
            line = line[:start] + line[end + 3:]
        # 4) 围栏开启
        if stripped.startswith("```"):
            fence = "```"
            continue
        if stripped.startswith("~~~"):
            fence = "~~~"
            continue
        # 5) 标题 -> 锚点(行内代码保留内容、去掉反引号, 与 GitHub slugger 一致)
        m = HEADING_RE.match(line)
        if m:
            heading = HEADING_CLOSE_RE.sub("", m.group(2)).replace("`", "")
            add_anchor(anchors, slugify(heading))
        for am in ANCHOR_TAG_RE.finditer(line):
            anchors.add(unquote(am.group(1)))
        # 6) 行内代码中的链接不渲染, 剔除后再提取
        visible = INLINE_CODE_RE.sub("", line)
        for lm in LINK_RE.finditer(visible):
            links.append((lineno, lm.group(1)))
    return anchors, links


anchor_cache = {}


def get_anchors(path):
    key = str(path)
    if key not in anchor_cache:
        anchor_cache[key] = scan_markdown(path)[0]
    return anchor_cache[key]


md_files = sorted(
    p for p in ROOT.rglob("*.md")
    if not (PRUNE_DIRS & set(p.relative_to(ROOT).parts[:-1]))
)

for md in md_files:
    stats["files"] += 1
    rel = md.relative_to(ROOT).as_posix()
    try:
        anchors, links = scan_markdown(md)
    except OSError as e:
        errors.append(f"{rel}: 无法读取: {e}")
        continue
    for lineno, target in links:
        stats["links"] += 1
        if not target:
            errors.append(f"{rel}:{lineno}: 空链接目标")
            continue
        if " " in target:
            errors.append(f"{rel}:{lineno}: 链接目标含空格(GitHub 不渲染, 请改用 %20): {target}")
            continue
        if SCHEME_RE.match(target) or target.startswith("/"):
            stats["skipped"] += 1
            continue
        path_part, _, frag = target.partition("#")
        path_part = unquote(path_part)
        frag = unquote(frag)
        if not path_part:  # 同页锚点
            if frag and frag not in anchors:
                errors.append(f"{rel}:{lineno}: 锚点不存在: #{frag}")
            continue
        resolved = (md.parent / path_part).resolve()
        if not resolved.exists():
            wiki_page = Path(str(resolved) + ".md")
            if wiki_page.exists():  # GitHub Wiki 无扩展名页引用
                resolved = wiki_page
            else:
                errors.append(f"{rel}:{lineno}: 链接目标不存在: {target}")
                continue
        if frag and resolved.suffix.lower() == ".md":
            if frag not in get_anchors(resolved):
                errors.append(f"{rel}:{lineno}: 跨文件锚点不存在: {target}")

print(f"链接统计: {stats['files']} 个文件, 提取 {stats['links']} 条链接(其中 {stats['skipped']} 条外部/绝对路径跳过)")
if errors:
    print(f"\n❌ {len(errors)} 个错误:")
    for e in errors[:50]:
        print(f"  - {e}")
    sys.exit(1)
print("\n✅ Markdown 链接检查通过")
