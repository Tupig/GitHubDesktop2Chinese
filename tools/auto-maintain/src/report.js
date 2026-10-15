import { buildDraftLine } from './extract-new.js';

// 表格最多渲染行数; 超出部分以说明行代替。
// 目的: 防止 Issue 正文超出 GitHub 65536 字符上限导致创建/更新失败(恰在最需要报告时丢失)。
// #146: 导出供 tools-self-test.mjs 引用, 测试不再隐式绑定常量数值(改常量即碎且碎法误导)
export const MAX_FAILED_ROWS = 150;
export const MAX_CANDIDATE_ROWS = 200;

// markdown 单元格纯文本转义: | 断表格列, 反引号开 inline code 均会破坏渲染。
// 反引号以全角 ˋ 替代(仅展示层, 不影响复制使用的内容)。
function mdText(s) {
  return String(s).replace(/\|/g, '\\|').replace(/`/g, 'ˋ');
}

// markdown 行内代码: 内容含反引号时用双反引号定界(CommonMark 支持);
// 内容含连续反引号(极罕见)时退回全角替代, 保证表格不被截断。
function mdCode(s) {
  const t = String(s).replace(/\|/g, '\\|');
  if (!t.includes('`')) return '`' + t + '`';
  if (!t.includes('``')) return '``' + t + '``';
  return '`' + t.replace(/`/g, 'ˋ') + '`';
}

export function renderMarkdown(report) {
  const lines = [];
  lines.push(`# 自动维护报告`);
  lines.push('');
  lines.push(`- GitHub Desktop 版本: ${report.version}`);
  lines.push(`- 生成时间: ${report.generatedAt}`);
  lines.push('');
  if (report.checks) {
    const { total, ok, failedCount, failed } = report.checks;
    lines.push(`## 失效检测`);
    lines.push('');
    lines.push(`总映射项 ${total}，有效 ${ok}，失效 ${failedCount}。`);
    // #137: 结构化统计 meta 行(HTML 注释, Markdown 渲染不可见):
    // report-issue.js 优先解析此行, 与上方中文展示文案解耦, 文案微调不再静默劣化自动关单逻辑
    lines.push(`<!-- stats: total=${total} ok=${ok} failed=${failedCount} -->`);
    lines.push('');
    lines.push(`| 数组 | 序号 | 原因 | 正则 |`);
    lines.push(`| --- | --- | --- | --- |`);
    for (const f of failed.slice(0, MAX_FAILED_ROWS)) {
      const reason = f.errors.map(e => e.reason).join('; ');
      lines.push(`| ${mdText(f.array)} | ${f.index} | ${mdText(reason)} | ${mdCode(f.errors[0].pattern)} |`);
    }
    if (failed.length > MAX_FAILED_ROWS) {
      lines.push(`| … | … | … | 其余 ${failed.length - MAX_FAILED_ROWS} 条失效项未展示，本地运行 \`npm run all\` 查看完整报告 |`);
    }
    lines.push('');
    const warnList = report.checks.warnings ?? [];
    if (warnList.length > 0) {
      lines.push(`### 告警（不阻断）`);
      lines.push('');
      // #146: 稳定语义标记(HTML 注释, 渲染不可见)——测试断言此标记而非标题文案, 措辞微调不再碎测试
      lines.push(`<!-- section: warnings -->`);
      for (const w of warnList) {
        lines.push(`- \`${mdText(w.array)}[${w.index}]\` ${mdText(w.reason)}: ${mdCode(w.pattern)}`);
      }
      lines.push('');
    }
  }
  if (report.simulation) {
    // 依序模拟检测段(#207): 隐性失效为 warning 级, 仅列异常条目供人工判断, 不参与 stats 失效统计
    const { total, hiddenCount, hidden, counts } = report.simulation;
    lines.push(`## 依序模拟检测`);
    lines.push('');
    lines.push(`依序应用 main/renderer/select 全部映射（main ${counts.main}，renderer ${counts.renderer}，select ${counts.select}，共 ${total} 条），隐性失效 ${hiddenCount} 条。`);
    // 结构化统计 meta 行(HTML 注释, 渲染不可见): 前缀 simulate 与失效检测的 stats 区分,
    // report-issue.js 的 extractFailedCount 仅匹配 stats 行, 不受此影响
    lines.push(`<!-- simulate: total=${total} hidden=${hiddenCount} -->`);
    lines.push('');
    if (hidden.length > 0) {
      lines.push(`| 数组 | 序号 | 原因 | 独立匹配 | 正则 |`);
      lines.push(`| --- | --- | --- | --- | --- |`);
      for (const h of hidden.slice(0, MAX_FAILED_ROWS)) {
        lines.push(`| ${mdText(h.array)} | ${h.index} | ${mdText(h.reason)} | ${h.origMatches} | ${mdCode(h.pattern)} |`);
      }
      if (hidden.length > MAX_FAILED_ROWS) {
        lines.push(`| … | … | … | … | 其余 ${hidden.length - MAX_FAILED_ROWS} 条隐性失效未展示，本地运行 \`simulate --verbose\` 查看完整列表 |`);
      }
      lines.push('');
    }
  }
  if (report.candidates) {
    const { candidates, patternsCount } = report.candidates;
    lines.push(`## 未翻译候选`);
    lines.push('');
    lines.push(`基于 ${patternsCount} 条映射正则，发现 ${candidates.length} 条未被覆盖的英文文案候选。`);
    lines.push('');
    lines.push(`> **如何补充翻译**：将草稿中的 \`【待翻译】\` 替换为中文译文后，加入对应数组`);
    lines.push(`> （来源含 \`main.js\` → 加入 \`main\`；来源含 \`renderer.js\` → 加入 \`renderer\`）。`);
    lines.push(`> 草稿中的查找项已按字面量转义；无法确认用途或无需翻译（如专有名词）的候选请跳过。`);
    lines.push('');
    lines.push(`| 来源 | 次数 | 候选文本 | JSON 草稿 |`);
    lines.push(`| --- | --- | --- | --- |`);
    for (const cand of candidates.slice(0, MAX_CANDIDATE_ROWS)) {
      const files = cand.files.join(', ');
      // 草稿需原样复制进 localization.json: 反引号用双定界保护, 不改写内容
      const draft = mdCode(buildDraftLine(cand.text));
      lines.push(`| ${mdText(files)} | ${cand.count} | ${mdText(cand.text)} | ${draft} |`);
    }
    if (candidates.length > MAX_CANDIDATE_ROWS) {
      lines.push(`| … | … | … | 其余 ${candidates.length - MAX_CANDIDATE_ROWS} 条候选未展示，本地运行 \`npm run all\` 查看完整报告 |`);
    }
    lines.push('');
  }
  return lines.join('\n');
}
