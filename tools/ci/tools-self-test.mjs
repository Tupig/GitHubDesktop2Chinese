// 自动维护工具单元测试(提取过滤 + 草稿生成 + 报告体积防护; 工作流 tools-test job 调用)
import { isLikelyUiText, buildDraftLine } from '../auto-maintain/src/extract-new.js';
import { renderMarkdown } from '../auto-maintain/src/report.js';
const cases = [
  ['Do not show this message again', true],
  ['into ', false],
  ['  leading space', false],
  ['h:mm aaa', false],
  ['[object Object]', false],
  ['rgba(255, 255, 255, 0.3)', false],
  ['GitHub Desktop', true],
];
let pass = 0;
for (const [s, exp] of cases) {
  const got = isLikelyUiText(s);
  if (got !== exp) { console.error('FAIL:', JSON.stringify(s), 'got', got, 'exp', exp); process.exit(1); }
  pass++;
}
// 草稿行必须是合法 JSON，且查找项按字面量匹配
for (const text of ['GitHub Desktop', 'Checkout commit?', 'No usage limit']) {
  const outer = JSON.parse('[' + buildDraftLine(text) + ']');
  const entry = outer[0];
  if (!Array.isArray(entry) || entry.length !== 2 || !entry.every(x => typeof x === 'string')) {
    console.error('FAIL: draft 非法', text); process.exit(1);
  }
  if (!new RegExp(entry[0]).test('"' + text + '"')) {
    console.error('FAIL: draft 模式匹配不到字面量', text); process.exit(1);
  }
  pass++;
}
// 报告体积防护: 大表应截断且总长不超 Issue 上限
const fakeFailed = Array.from({ length: 1000 }, (_, i) => ({ array: 'renderer', index: i, item: [], errors: [{ reason: 'not-found', pattern: 'pattern-' + i }] }));
const big = renderMarkdown({ version: 't', generatedAt: 't', checks: { total: 1000, ok: 0, failedCount: 1000, failed: fakeFailed }, candidates: { candidates: [], patternsCount: 0 } });
if (!big.includes('其余 850 条失效项未展示')) { console.error('FAIL: 失效表截断说明缺失'); process.exit(1); }
if (big.length >= 65536) { console.error('FAIL: 报告超出体积上限', big.length); process.exit(1); }
pass++;
console.log('单元测试通过:', pass, '项');
