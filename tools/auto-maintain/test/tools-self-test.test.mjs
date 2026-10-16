// 质检工具链单元测试（Vitest）
// 覆盖: 提取过滤 / 草稿生成 / 报告体积防护 / 告警通道 / 报告-关单跨文件契约 /
//       fetch.js 纯逻辑(#174, 含 #19/#132/#118 回归) / args.js 参数解析
// 运行: npm test（等价 vitest run）
import { describe, it, expect } from 'vitest';
import fs from 'node:fs';
import os from 'node:os';
import nodePath from 'node:path';
import { createRequire } from 'node:module';

import { isLikelyUiText, buildDraftLine } from '../src/extract-new.js';
// #146: 引用被测模块导出常量, 不在断言里重复绑定实现数值/字符串字面量
import { renderMarkdown, MAX_FAILED_ROWS } from '../src/report.js';
import { checkEntry, WARN_REASON_REDOS_SUSPECT } from '../src/check-invalid.js';
import {
  buildLatestReleaseUrl, parseVersionFromTag, pickWindowsAsset, retryWaitMsFromHeader,
  rangeResumeStart, is416SelfHeal, deriveCachePaths, extractSourceId,
  isZipComplete, sha256File, digestMismatch,
} from '../src/fetch.js';
// #174: index.js 参数解析抽 args.js, CLI 分支回归
import { parseArgs } from '../src/args.js';

// #137: renderMarkdown 真实输出喂 report-issue.js 解析——跨文件文本契约回归
const requireCjs = createRequire(import.meta.url);
const { extractFailedCount } = requireCjs('../../ci/report-issue.js');

describe('extract-new: 提取过滤（isLikelyUiText）', () => {
  const cases = [
    ['Do not show this message again', true],
    ['into ', false],
    ['  leading space', false],
    ['h:mm aaa', false],
    ['[object Object]', false],
    ['rgba(255, 255, 255, 0.3)', false],
    ['GitHub Desktop', true],
  ];
  it.each(cases)('isLikelyUiText(%j) === %s', (text, expected) => {
    expect(isLikelyUiText(text)).toBe(expected);
  });
});

describe('extract-new: 草稿行生成（buildDraftLine）', () => {
  it.each(['GitHub Desktop', 'Checkout commit?', 'No usage limit'])(
    '草稿行为合法 JSON 且查找项按字面量匹配: %s',
    (text) => {
      const outer = JSON.parse('[' + buildDraftLine(text) + ']');
      const entry = outer[0];
      expect(Array.isArray(entry)).toBe(true);
      expect(entry).toHaveLength(2);
      expect(entry.every((x) => typeof x === 'string')).toBe(true);
      expect(new RegExp(entry[0]).test('"' + text + '"')).toBe(true);
    },
  );
});

describe('report: 报告体积防护', () => {
  it('大表截断且总长不超 Issue 上限', () => {
    const fakeFailed = Array.from({ length: 1000 }, (_, i) => ({
      array: 'renderer', index: i, item: [], errors: [{ reason: 'not-found', pattern: 'pattern-' + i }],
    }));
    const big = renderMarkdown({
      version: 't', generatedAt: 't',
      checks: { total: 1000, ok: 0, failedCount: 1000, failed: fakeFailed },
      candidates: { candidates: [], patternsCount: 0 },
    });
    // #146: 截断条数引用导出常量计算, 改 MAX_FAILED_ROWS 测试自动跟随(不再隐式绑定 850)
    expect(big).toContain(`其余 ${1000 - MAX_FAILED_ROWS} 条失效项未展示`);
    expect(big.length).toBeLessThan(65536);
  });
});

describe('check-invalid: ReDoS 告警通道（仅告警不阻断）', () => {
  it('告警进入 checkEntry 结果并渲染进报告', () => {
    const warned = checkEntry('text', ['(a+)+x', 'y']);
    expect(warned.ok).toBe(false);
    expect(warned.warnings).toHaveLength(1);
    expect(warned.warnings[0].reason).toBe(WARN_REASON_REDOS_SUSPECT);

    const warnedMd = renderMarkdown({
      version: 't', generatedAt: 't',
      checks: {
        total: 1, ok: 0, failedCount: 1,
        failed: [{ array: 'main', index: 0, errors: warned.errors }],
        warnings: [{ array: 'main', index: 0, ...warned.warnings[0] }],
      },
      candidates: null,
    });
    // #146: 断言稳定语义标记而非"告警（不阻断）"标题文案, 措辞微调不再碎测试
    expect(warnedMd).toContain('<!-- section: warnings -->');
    expect(warnedMd).toContain(WARN_REASON_REDOS_SUSPECT);
  });
});

describe('report-issue: 报告文本契约（extractFailedCount）', () => {
  const statsReport = renderMarkdown({
    version: 't', generatedAt: 't',
    checks: { total: 120, ok: 111, failedCount: 9, failed: [] },
    candidates: { candidates: [], patternsCount: 0 },
  });

  it('meta 行解析', () => {
    expect(extractFailedCount(statsReport)).toEqual({ failed: '9', source: 'meta' });
  });

  it('文案微调后 meta 行仍可解析', () => {
    const retuned = statsReport.replace('总映射项 120，有效 111，失效 9。', '总映射项 120 项，有效 111 项，失效 9 项。');
    expect(extractFailedCount(retuned)).toEqual({ failed: '9', source: 'meta' });
  });

  it('旧格式（无 meta 行）后备中文文案解析仍工作', () => {
    const legacyOnly = statsReport.replace(/<!-- stats:[^\n]*-->\n/, '');
    expect(extractFailedCount(legacyOnly)).toEqual({ failed: '9', source: 'legacy-text' });
  });

  it('零失效: 自动关单链路精确', () => {
    const zeroReport = renderMarkdown({
      version: 't', generatedAt: 't',
      checks: { total: 10, ok: 10, failedCount: 0, failed: [] },
      candidates: null,
    });
    const parsed = extractFailedCount(zeroReport);
    expect(parsed.failed).toBe('0');
    expect(Number(parsed.failed)).toBe(0);
  });

  it('完全无统计信息: ? 且不误判为无失效', () => {
    const parsed = extractFailedCount('no stats at all');
    expect(parsed.failed).toBe('?');
    expect(Number(parsed.failed)).not.toBe(0);
  });
});

describe('fetch.js: URL / 版本号 / 资产挑选 / 限流等待', () => {
  it('API 查询 URL 契约（端点/仓库任一漂移即时暴露）', () => {
    expect(buildLatestReleaseUrl()).toBe('https://api.github.com/repos/desktop/desktop/releases/latest');
  });

  it('tag -> 版本号: release- 前缀剥离, 无前缀原样保留', () => {
    expect(parseVersionFromTag('release-3.6.6')).toBe('3.6.6');
    expect(parseVersionFromTag('3.6.6')).toBe('3.6.6');
  });

  it('资产挑选: 只认 Windows x64 完整包 nupkg, 非数组/无匹配返回 null', () => {
    const assetList = [
      { name: 'GitHub.Desktop-x64.zip' },
      { name: 'GitHubDesktop-3.6.6-x64-full.nupkg', browser_download_url: 'u2' },
      { name: 'RELEASES' },
    ];
    const picked = pickWindowsAsset(assetList);
    expect(picked).toBeTruthy();
    expect(picked.name).toBe('GitHubDesktop-3.6.6-x64-full.nupkg');
    expect(pickWindowsAsset([{ name: 'GitHub.Desktop-setup-x64.exe' }])).toBeNull();
    expect(pickWindowsAsset('not-an-array')).toBeNull();
  });

  it('限流 Retry-After(#132): 数值换算毫秒, 90s 封顶, 缺失/非法/非正数回落 undefined', () => {
    expect(retryWaitMsFromHeader('30')).toBe(30_000);
    expect(retryWaitMsFromHeader('120')).toBe(90_000);
    expect(retryWaitMsFromHeader('abc')).toBeUndefined();
    expect(retryWaitMsFromHeader(null)).toBeUndefined();
    expect(retryWaitMsFromHeader('0')).toBeUndefined();
  });
});

describe('fetch.js: 断点续传 416 自愈与 Content-Range 校验（#19 回归）', () => {
  it('416 自愈判定', () => {
    expect(is416SelfHeal(416, true)).toBe(true);
    expect(is416SelfHeal(416, false)).toBe(false);
    expect(is416SelfHeal(206, true)).toBe(false);
    expect(is416SelfHeal(200, true)).toBe(false);
  });

  it('Content-Range 起始位置解析（无法解析返回 null, 维持 fail-open）', () => {
    expect(rangeResumeStart('bytes 1234-5678/9012')).toBe(1234);
    expect(rangeResumeStart('bytes 999-/x')).toBe(999);
    expect(rangeResumeStart('garbage')).toBeNull();
    expect(rangeResumeStart(null)).toBeNull();
  });
});

describe('fetch.js: 缓存路径推导与提取复用标记', () => {
  it('下载缓存路径推导', () => {
    const cachePaths = deriveCachePaths(nodePath.join(os.tmpdir(), 'ghd2c-selftest'));
    expect(nodePath.basename(cachePaths.zipPath)).toBe('github-desktop.nupkg');
    expect(cachePaths.tmpPath).toBe(cachePaths.zipPath + '.part');
    expect(cachePaths.tmpUrlPath).toBe(cachePaths.tmpPath + '.url');
  });

  it('提取复用标记: 版本变化标记即变, 版本缺省回退包名', () => {
    expect(extractSourceId('3.6.6', 'pkg.nupkg', 100)).toBe('3.6.6:100');
    expect(extractSourceId('3.6.6', 'pkg.nupkg', 100)).not.toBe(extractSourceId('3.7.0', 'pkg.nupkg', 100));
    expect(extractSourceId('', 'legacy.nupkg', 7)).toBe('legacy.nupkg:7');
  });
});

describe('fetch.js: 文件级完整性（EOCD / sha256 / digest）', () => {
  it('EOCD 完整性判定 + sha256 + digest 校验', async () => {
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
      expect(isZipComplete(zipOk)).toBe(true);
      expect(isZipComplete(zipBad)).toBe(false);
      expect(isZipComplete(zipTiny)).toBe(false);
      expect(isZipComplete(nodePath.join(tmpRoot, 'nonexistent.nupkg'))).toBe(false);

      const abcFile = nodePath.join(tmpRoot, 'abc.txt');
      fs.writeFileSync(abcFile, 'abc');
      const ABC_SHA256 = 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad';
      // 流式哈希与已知值对齐
      expect(await sha256File(abcFile)).toBe(ABC_SHA256);

      // digest 校验(#118): 一致放行(false), 不一致拦截(true), 缺失/格式异常放行(false, 维持基线)
      const origWarn = console.warn;
      console.warn = () => {};
      try {
        expect(await digestMismatch(abcFile, `sha256:${ABC_SHA256}`)).toBe(false);
        expect(await digestMismatch(abcFile, `sha256:${'0'.repeat(64)}`)).toBe(true);
        expect(await digestMismatch(abcFile, null)).toBe(false);
        expect(await digestMismatch(abcFile, 'md5:deadbeef')).toBe(false);
      } finally {
        console.warn = origWarn;
      }
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });
});

describe('args.js: index.js 参数解析分支（#174）', () => {
  it('默认值: 无参数 → all, top=60, 开关全 false; --json 默认指向仓库根映射', () => {
    const parsedDefault = parseArgs([]);
    expect(JSON.stringify(parsedDefault.cmds)).toBe('["all"]');
    expect(parsedDefault.top).toBe(60);
    expect(parsedDefault.writeReport).toBeFalsy();
    expect(parsedDefault.keepJs).toBeFalsy();
    expect(parsedDefault.noColor).toBeFalsy();
    expect(parsedDefault.json.replaceAll('\\', '/').endsWith('/json/localization.json')).toBe(true);
  });

  it('子命令与全量选项: 组合/覆盖路径、--top 0 为合法非负整数', () => {
    const parsedFull = parseArgs(['check', 'extract', '--json', '/x/y.json', '--workdir', '/w',
      '--top', '0', '--write-report', '--keep-js', '--no-color']);
    expect(parsedFull.cmds.join(',')).toBe('check,extract');
    expect(parsedFull.json).toBe('/x/y.json');
    expect(parsedFull.workdir).toBe('/w');
    expect(parsedFull.top).toBe(0);
    expect(parsedFull.writeReport).toBe(true);
    expect(parsedFull.keepJs).toBe(true);
    expect(parsedFull.noColor).toBe(true);
  });

  it('异常分支: 选项缺值 / --top 非法 / 未知参数均抛错', () => {
    expect(() => parseArgs(['--json'])).toThrow();
    expect(() => parseArgs(['--workdir'])).toThrow();
    expect(() => parseArgs(['--top'])).toThrow();
    expect(() => parseArgs(['--top', 'abc'])).toThrow();
    expect(() => parseArgs(['--top', '-1'])).toThrow();
    expect(() => parseArgs(['--unknown-arg'])).toThrow();
  });
});
