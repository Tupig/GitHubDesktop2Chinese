// 自动维护工具单元测试(提取过滤 + 草稿生成 + 报告体积防护 + 告警通道; 工作流 tools-test job 调用)
import { isLikelyUiText, buildDraftLine } from '../auto-maintain/src/extract-new.js';
// #146: 引用被测模块导出常量, 不在断言里重复绑定实现数值/字符串字面量
import { renderMarkdown, MAX_FAILED_ROWS } from '../auto-maintain/src/report.js';
import { checkEntry, WARN_REASON_REDOS_SUSPECT } from '../auto-maintain/src/check-invalid.js';
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
// #146: 截断条数引用导出常量计算, 改 MAX_FAILED_ROWS 测试自动跟随(不再隐式绑定 850)
if (!big.includes(`其余 ${1000 - MAX_FAILED_ROWS} 条失效项未展示`)) { console.error('FAIL: 失效表截断说明缺失'); process.exit(1); }
if (big.length >= 65536) { console.error('FAIL: 报告超出体积上限', big.length); process.exit(1); }
pass++;
// 告警通道: ReDoS 启发式仅告警不阻断, 且进入报告渲染
const warned = checkEntry('text', ['(a+)+x', 'y']);
if (warned.ok !== false || warned.warnings.length !== 1 || warned.warnings[0].reason !== WARN_REASON_REDOS_SUSPECT) {
  console.error('FAIL: ReDoS 告警通道异常', JSON.stringify(warned)); process.exit(1);
}
const warnedMd = renderMarkdown({ version: 't', generatedAt: 't', checks: { total: 1, ok: 0, failedCount: 1, failed: [{ array: 'main', index: 0, errors: warned.errors }], warnings: [{ array: 'main', index: 0, ...warned.warnings[0] }] }, candidates: null });
// #146: 断言稳定语义标记而非"告警（不阻断）"标题文案, 措辞微调不再碎测试
if (!warnedMd.includes('<!-- section: warnings -->') || !warnedMd.includes(WARN_REASON_REDOS_SUSPECT)) {
  console.error('FAIL: 报告未渲染告警段'); process.exit(1);
}
pass++;
// #137: renderMarkdown 真实输出喂 report-issue.js 解析——跨文件文本契约回归
// (修复前: 文案微调即静默劣化自动关单逻辑, 该链路零测试覆盖)
import { createRequire } from 'node:module';
const requireCjs = createRequire(import.meta.url);
const { extractFailedCount } = requireCjs('./report-issue.js');
const statsReport = renderMarkdown({ version: 't', generatedAt: 't', checks: { total: 120, ok: 111, failedCount: 9, failed: [] }, candidates: { candidates: [], patternsCount: 0 } });
const metaParsed = extractFailedCount(statsReport);
if (metaParsed.failed !== '9' || metaParsed.source !== 'meta') {
  console.error('FAIL: meta 行解析失败', JSON.stringify(metaParsed)); process.exit(1);
}
// 文案微调后(模拟"总映射项 120 项，…")meta 行仍可解析——解耦验证
const retuned = statsReport.replace('总映射项 120，有效 111，失效 9。', '总映射项 120 项，有效 111 项，失效 9 项。');
const retunedParsed = extractFailedCount(retuned);
if (retunedParsed.failed !== '9' || retunedParsed.source !== 'meta') {
  console.error('FAIL: 文案微调后 meta 解析失败', JSON.stringify(retunedParsed)); process.exit(1);
}
// 旧格式(无 meta 行)后备中文文案解析仍工作
const legacyOnly = statsReport.replace(/<!-- stats:[^\n]*-->\n/, '');
const legacyParsed = extractFailedCount(legacyOnly);
if (legacyParsed.failed !== '9' || legacyParsed.source !== 'legacy-text') {
  console.error('FAIL: 旧格式后备解析失败', JSON.stringify(legacyParsed)); process.exit(1);
}
// 零失效: noFailures 自动关单链路精确
const zeroReport = renderMarkdown({ version: 't', generatedAt: 't', checks: { total: 10, ok: 10, failedCount: 0, failed: [] }, candidates: null });
const zeroParsed = extractFailedCount(zeroReport);
if (zeroParsed.failed !== '0' || Number(zeroParsed.failed) !== 0) {
  console.error('FAIL: 零失效解析异常', JSON.stringify(zeroParsed)); process.exit(1);
}
// 完全无统计信息: '?' 且不误判为无失效
const noneParsed = extractFailedCount('no stats at all');
if (noneParsed.failed !== '?' || Number(noneParsed.failed) === 0) {
  console.error('FAIL: 无统计降级异常', JSON.stringify(noneParsed)); process.exit(1);
}
pass++;
console.log('单元测试通过:', pass, '项');
