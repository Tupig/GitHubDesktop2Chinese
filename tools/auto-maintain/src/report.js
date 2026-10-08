import { buildDraftLine } from './extract-new.js';

// 表格最多渲染行数; 超出部分以说明行代替。
// 目的: 防止 Issue 正文超出 GitHub 65536 字符上限导致创建/更新失败(恰在最需要报告时丢失)。
const MAX_FAILED_ROWS = 150;
const MAX_CANDIDATE_ROWS = 200;

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
    lines.push('');
    lines.push(`| 数组 | 序号 | 原因 | 正则 |`);
    lines.push(`| --- | --- | --- | --- |`);
    for (const f of failed.slice(0, MAX_FAILED_ROWS)) {
      const reason = f.errors.map(e => (e.reason === 'regex-error' ? 'regex-error' : 'not-found')).join('; ');
      lines.push(`| ${f.array} | ${f.index} | ${reason} | \`${f.errors[0].pattern.replace(/\|/g, '\\|')}\` |`);
    }
    if (failed.length > MAX_FAILED_ROWS) {
      lines.push(`| … | … | … | 其余 ${failed.length - MAX_FAILED_ROWS} 条失效项未展示，本地运行 \`npm run all\` 查看完整报告 |`);
    }
    lines.push('');
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
      const draft = `\`${buildDraftLine(cand.text).replace(/\|/g, '\\|')}\``;
      lines.push(`| ${files} | ${cand.count} | ${cand.text.replace(/\|/g, '\\|')} | ${draft} |`);
    }
    if (candidates.length > MAX_CANDIDATE_ROWS) {
      lines.push(`| … | … | … | 其余 ${candidates.length - MAX_CANDIDATE_ROWS} 条候选未展示，本地运行 \`npm run all\` 查看完整报告 |`);
    }
    lines.push('');
  }
  return lines.join('\n');
}
