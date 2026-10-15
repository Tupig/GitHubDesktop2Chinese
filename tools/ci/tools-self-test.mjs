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

// ── #174: fetch.js 纯逻辑回归（URL 构造 / 版本号解析 / 资产挑选 / 限流重试等待）──
import {
  buildLatestReleaseUrl, parseVersionFromTag, pickWindowsAsset, retryWaitMsFromHeader,
  rangeResumeStart, is416SelfHeal, deriveCachePaths, extractSourceId,
  isZipComplete, sha256File, digestMismatch,
} from '../auto-maintain/src/fetch.js';
// #174: index.js 参数解析抽 args.js, CLI 分支回归
import { parseArgs } from '../auto-maintain/src/args.js';
import fs from 'node:fs';
import os from 'node:os';
import nodePath from 'node:path';

// API 查询 URL 契约: 端点/仓库任一漂移(如换 API 版本)即时暴露
if (buildLatestReleaseUrl() !== 'https://api.github.com/repos/desktop/desktop/releases/latest') {
  console.error('FAIL: release 查询 URL 构造异常', buildLatestReleaseUrl()); process.exit(1);
}
pass++;
// tag -> 版本号: release- 前缀剥离, 无前缀原样保留
if (parseVersionFromTag('release-3.6.6') !== '3.6.6' || parseVersionFromTag('3.6.6') !== '3.6.6') {
  console.error('FAIL: tag 版本号解析异常'); process.exit(1);
}
pass++;
// 资产挑选: 只认 Windows x64 完整包 nupkg, macOS zip/杂项排除, 非数组/无匹配返回 null
const assetList = [
  { name: 'GitHub.Desktop-x64.zip' },
  { name: 'GitHubDesktop-3.6.6-x64-full.nupkg', browser_download_url: 'u2' },
  { name: 'RELEASES' },
];
const picked = pickWindowsAsset(assetList);
if (!picked || picked.name !== 'GitHubDesktop-3.6.6-x64-full.nupkg') {
  console.error('FAIL: Windows 资产挑选异常', JSON.stringify(picked)); process.exit(1);
}
if (pickWindowsAsset([{ name: 'GitHub.Desktop-setup-x64.exe' }]) !== null ||
    pickWindowsAsset('not-an-array') !== null) {
  console.error('FAIL: 资产挑选未排除非目标/非法输入'); process.exit(1);
}
pass++;
// 限流 Retry-After(#132): 数值换算毫秒, 90s 封顶, 缺失/非法/非正数回落 undefined
if (retryWaitMsFromHeader('30') !== 30_000 || retryWaitMsFromHeader('120') !== 90_000 ||
    retryWaitMsFromHeader('abc') !== undefined || retryWaitMsFromHeader(null) !== undefined ||
    retryWaitMsFromHeader('0') !== undefined) {
  console.error('FAIL: Retry-After 重试等待计算异常'); process.exit(1);
}
pass++;

// ── #174: fetch.js 断点续传 416 自愈与 Content-Range 校验（#19 回归）──
if (!is416SelfHeal(416, true) || is416SelfHeal(416, false) ||
    is416SelfHeal(206, true) || is416SelfHeal(200, true)) {
  console.error('FAIL: 416 自愈判定异常'); process.exit(1);
}
pass++;
// Content-Range 起始位置解析: 无法解析返回 null(维持原 fail-open 语义, 不误判为错位)
if (rangeResumeStart('bytes 1234-5678/9012') !== 1234 ||
    rangeResumeStart('bytes 999-/x') !== 999 ||
    rangeResumeStart('garbage') !== null || rangeResumeStart(null) !== null) {
  console.error('FAIL: Content-Range 断点位置解析异常'); process.exit(1);
}
pass++;

// ── #174: fetch.js 下载缓存路径推导与提取复用标记（版本比较）──
const cachePaths = deriveCachePaths(nodePath.join(os.tmpdir(), 'ghd2c-selftest'));
if (nodePath.basename(cachePaths.zipPath) !== 'github-desktop.nupkg' ||
    cachePaths.tmpPath !== cachePaths.zipPath + '.part' ||
    cachePaths.tmpUrlPath !== cachePaths.tmpPath + '.url') {
  console.error('FAIL: 下载缓存路径推导异常', JSON.stringify(cachePaths)); process.exit(1);
}
pass++;
// 提取复用标记: 版本变化标记即变(缓存不复用), 版本缺省回退包名
if (extractSourceId('3.6.6', 'pkg.nupkg', 100) !== '3.6.6:100' ||
    extractSourceId('3.6.6', 'pkg.nupkg', 100) === extractSourceId('3.7.0', 'pkg.nupkg', 100) ||
    extractSourceId('', 'legacy.nupkg', 7) !== 'legacy.nupkg:7') {
  console.error('FAIL: 提取复用标记(版本比较)异常'); process.exit(1);
}
pass++;

// ── #174: fetch.js 文件级完整性（EOCD / sha256 / digest, 临时文件, 不联网）──
const tmpRoot = fs.mkdtempSync(nodePath.join(os.tmpdir(), 'ghd2c-selftest-'));
try {
  const EOCD = Buffer.from([0x50, 0x4b, 0x05, 0x06]);
  const zipOk = nodePath.join(tmpRoot, 'ok.nupkg');
  fs.writeFileSync(zipOk, Buffer.concat([Buffer.alloc(30, 1), EOCD]));
  const zipBad = nodePath.join(tmpRoot, 'bad.nupkg');
  fs.writeFileSync(zipBad, Buffer.alloc(30, 1));
  const zipTiny = nodePath.join(tmpRoot, 'tiny.nupkg');
  fs.writeFileSync(zipTiny, Buffer.alloc(10, 1));
  // 完整=尾部窗口含 EOCD 签名; 截断/过短/不存在均为 false
  if (!isZipComplete(zipOk) || isZipComplete(zipBad) || isZipComplete(zipTiny) ||
      isZipComplete(nodePath.join(tmpRoot, 'nonexistent.nupkg'))) {
    console.error('FAIL: EOCD 完整性判定异常'); process.exit(1);
  }
  pass++;
  const abcFile = nodePath.join(tmpRoot, 'abc.txt');
  fs.writeFileSync(abcFile, 'abc');
  const ABC_SHA256 = 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad';
  // 流式哈希与已知值对齐
  if ((await sha256File(abcFile)) !== ABC_SHA256) {
    console.error('FAIL: sha256File 计算异常'); process.exit(1);
  }
  pass++;
  // digest 校验(#118): 一致放行(false), 不一致拦截(true), 缺失/格式异常放行(false, 维持基线)
  const origWarn = console.warn;
  console.warn = () => {};
  try {
    if (await digestMismatch(abcFile, `sha256:${ABC_SHA256}`)) {
      console.error('FAIL: digest 一致被误判'); process.exit(1);
    }
    if (!(await digestMismatch(abcFile, `sha256:${'0'.repeat(64)}`))) {
      console.error('FAIL: digest 不一致未被拦截'); process.exit(1);
    }
    if (await digestMismatch(abcFile, null) || await digestMismatch(abcFile, 'md5:deadbeef')) {
      console.error('FAIL: digest 缺失/格式异常应放行'); process.exit(1);
    }
  } finally {
    console.warn = origWarn;
  }
  pass++;
} finally {
  fs.rmSync(tmpRoot, { recursive: true, force: true });
}

// ── #174: index.js 参数解析分支（args.js）──
// 默认值: 无参数 → all, top=60, 开关全 false; --json 默认按脚本位置推导指向仓库根映射
const parsedDefault = parseArgs([]);
if (JSON.stringify(parsedDefault.cmds) !== '["all"]' || parsedDefault.top !== 60 ||
    parsedDefault.writeReport || parsedDefault.keepJs || parsedDefault.noColor ||
    !parsedDefault.json.replaceAll('\\', '/').endsWith('/json/localization.json')) {
  console.error('FAIL: 参数默认值异常', JSON.stringify(parsedDefault)); process.exit(1);
}
pass++;
// 子命令与全量选项: 组合/覆盖路径、--top 0 为合法非负整数
const parsedFull = parseArgs(['check', 'extract', '--json', '/x/y.json', '--workdir', '/w',
  '--top', '0', '--write-report', '--keep-js', '--no-color']);
if (parsedFull.cmds.join(',') !== 'check,extract' || parsedFull.json !== '/x/y.json' ||
    parsedFull.workdir !== '/w' || parsedFull.top !== 0 || !parsedFull.writeReport ||
    !parsedFull.keepJs || !parsedFull.noColor) {
  console.error('FAIL: 参数选项解析异常', JSON.stringify(parsedFull)); process.exit(1);
}
pass++;
// 异常分支: 选项缺值 / --top 非法 / 未知参数均抛错
const expectThrow = (fn) => { try { fn(); return false; } catch { return true; } };
if (!expectThrow(() => parseArgs(['--json'])) || !expectThrow(() => parseArgs(['--workdir'])) ||
    !expectThrow(() => parseArgs(['--top'])) || !expectThrow(() => parseArgs(['--top', 'abc'])) ||
    !expectThrow(() => parseArgs(['--top', '-1'])) || !expectThrow(() => parseArgs(['--unknown-arg']))) {
  console.error('FAIL: 非法参数未抛错'); process.exit(1);
}
pass++;
console.log('单元测试通过:', pass, '项');
