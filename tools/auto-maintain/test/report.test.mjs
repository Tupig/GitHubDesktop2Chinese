// report.js 单元测试（Vitest）
// 覆盖 renderMarkdown 各段渲染分支：失效表/截断/告警段/依序模拟段/候选段/markdown 转义
import { describe, it, expect } from 'vitest';
import { renderMarkdown, MAX_FAILED_ROWS, MAX_CANDIDATE_ROWS } from '../src/report.js';
import { WARN_REASON_REDOS_SUSPECT } from '../src/check-invalid.js';

function base(overrides = {}) {
  return {
    version: '3.6.7',
    generatedAt: '2026-10-16T00:00:00Z',
    checks: { total: 3, ok: 2, failedCount: 1, failed: [] },
    ...overrides,
  };
}

describe('renderMarkdown：头部与失效检测段', () => {
  it('输出标题/版本/生成时间与结构化 stats meta 行', () => {
    const md = renderMarkdown(base());
    expect(md).toContain('# 自动维护报告');
    expect(md).toContain('- GitHub Desktop 版本: 3.6.7');
    expect(md).toContain('- 生成时间: 2026-10-16T00:00:00Z');
    expect(md).toContain('总映射项 3，有效 2，失效 1。');
    expect(md).toContain('<!-- stats: total=3 ok=2 failed=1 -->');
  });

  it('失效行渲染并转义表格分隔符（正则列以行内代码定界）', () => {
    const md = renderMarkdown(base({
      checks: {
        total: 2, ok: 1, failedCount: 1,
        failed: [{ array: 'main', index: 7, errors: [{ reason: 'not-found', pattern: 'a|b`c' }] }],
      },
    }));
    expect(md).toContain('| main | 7 | not-found |');
    expect(md).toContain('a\\|b');   // | 在单元格/行内代码内均转义
    expect(md).toContain('``a\\|b`c``'); // 含反引号 → 双反引号定界
  });

  it('失效超过 MAX_FAILED_ROWS 时输出截断说明行', () => {
    const failed = Array.from({ length: MAX_FAILED_ROWS + 5 }, (_, i) => ({
      array: 'main', index: i, errors: [{ reason: 'not-found', pattern: 'p' + i }],
    }));
    const md = renderMarkdown(base({
      checks: { total: failed.length, ok: 0, failedCount: failed.length, failed },
    }));
    expect(md).toContain(`其余 5 条失效项未展示`);
  });

  it('无 checks 段时跳过失效检测章节', () => {
    const md = renderMarkdown({ version: 'v', generatedAt: 't' });
    expect(md).not.toContain('## 失效检测');
    expect(md).not.toContain('<!-- stats:');
  });

  it('告警段带稳定语义标记，且 warnings 缺省时整段省略', () => {
    const withWarn = renderMarkdown(base({
      checks: {
        total: 1, ok: 0, failedCount: 1, failed: [],
        warnings: [{ array: 'renderer', index: 3, reason: WARN_REASON_REDOS_SUSPECT, pattern: '(a+)+' }],
      },
    }));
    expect(withWarn).toContain('### 告警（不阻断）');
    expect(withWarn).toContain('<!-- section: warnings -->');
    expect(withWarn).toContain('renderer[3]');
    expect(renderMarkdown(base()).includes('<!-- section: warnings -->')).toBe(false);
  });
});

describe('renderMarkdown：依序模拟段（#207）', () => {
  it('有隐性失效时渲染表格与 simulate meta 行', () => {
    const md = renderMarkdown(base({
      simulation: {
        total: 10, hiddenCount: 1, counts: { main: 8, renderer: 1, select: 1 },
        hidden: [{ array: 'main', index: 2, reason: 'preempted', origMatches: 1, pattern: 'Edit model' }],
      },
    }));
    expect(md).toContain('## 依序模拟检测');
    expect(md).toContain('main 8，renderer 1，select 1，共 10 条');
    expect(md).toContain('<!-- simulate: total=10 hidden=1 -->');
    expect(md).toContain('| main | 2 | preempted | 1 | `Edit model` |');
  });

  it('无隐性失效时不渲染表格但保留统计行', () => {
    const md = renderMarkdown(base({
      simulation: { total: 5, hiddenCount: 0, counts: { main: 5, renderer: 0, select: 0 }, hidden: [] },
    }));
    expect(md).toContain('隐性失效 0 条');
    expect(md).not.toContain('| 数组 | 序号 | 原因 | 独立匹配 | 正则 |');
  });

  it('隐性失效超过 MAX_FAILED_ROWS 时输出截断说明行', () => {
    const hidden = Array.from({ length: MAX_FAILED_ROWS + 2 }, (_, i) => ({
      array: 'main', index: i, reason: 'preempted', origMatches: 1, pattern: 'p' + i,
    }));
    const md = renderMarkdown(base({
      simulation: {
        total: hidden.length, hiddenCount: hidden.length,
        counts: { main: hidden.length, renderer: 0, select: 0 }, hidden,
      },
    }));
    expect(md).toContain('其余 2 条隐性失效未展示');
  });
});

describe('renderMarkdown：未翻译候选段', () => {
  it('渲染候选表与 JSON 草稿（草稿原样不改写）', () => {
    const md = renderMarkdown(base({
      candidates: { candidates: [{ text: 'New Alpha', count: 3, files: ['main.js', 'renderer.js'] }], patternsCount: 12 },
    }));
    expect(md).toContain('## 未翻译候选');
    expect(md).toContain('基于 12 条映射正则，发现 1 条未被覆盖的英文文案候选。');
    expect(md).toContain('| main.js, renderer.js | 3 | New Alpha |');
    expect(md).toContain('【待翻译】');
  });

  it('候选超过 MAX_CANDIDATE_ROWS 时输出截断说明行', () => {
    const candidates = Array.from({ length: MAX_CANDIDATE_ROWS + 7 }, (_, i) => ({
      text: 'Candidate ' + i, count: 1, files: ['main.js'],
    }));
    const md = renderMarkdown(base({ candidates: { candidates, patternsCount: 1 } }));
    expect(md).toContain('其余 7 条候选未展示');
  });

  it('无候选段时省略该章节', () => {
    expect(renderMarkdown(base()).includes('## 未翻译候选')).toBe(false);
  });
});
