// simulate.js 单元测试（Vitest）
// 覆盖依序模拟检测的纯函数与主流程（#207）
import { describe, it, expect } from 'vitest';
import {
  isPlainLiteral, fillPlaceholders, countMatches, simulateApply,
  SIM_REASON_PREEMPTED, SIM_REASON_GATE_MISS, SIM_REASON_GATE_LEFTOVER,
} from '../src/simulate.js';

describe('isPlainLiteral：字面量判定', () => {
  it('无正则元字符为字面量，含元字符/空串不是', () => {
    expect(isPlainLiteral('Checkout commit')).toBe(true);
    expect(isPlainLiteral('a.b')).toBe(false);
    expect(isPlainLiteral('a+b')).toBe(false);
    expect(isPlainLiteral('')).toBe(false);
  });
});

describe('fillPlaceholders：第三参数回填', () => {
  it('回填 #{n} 并转义 $（避免 replace 语义被误解）', () => {
    const r = fillPlaceholders('key=value', '(\\w+)=(\\w+)', '$2:$1 -> #{1}#{2}');
    expect(r.status).toBe('filled');
    expect(r.translation).toBe('$2:$1 -> keyvalue');
  });

  it('第三参数非法 → regex-error；未匹配 → miss；残留占位符 → leftover', () => {
    expect(fillPlaceholders('x', '(', '#{1}').status).toBe('regex-error');
    expect(fillPlaceholders('x', 'zzz(\\d+)', '#{1}').status).toBe('miss');
    expect(fillPlaceholders('v1', 'v(\\d+)', '#{9}').status).toBe('leftover');
  });
});

describe('countMatches：非重叠匹配计数', () => {
  it('计数正确，非法正则返回 null', () => {
    expect(countMatches('aaa', 'a')).toBe(3);
    expect(countMatches('a.b.c', '\\.')).toBe(2);
    expect(countMatches('x', '(')).toBeNull();
  });
});

describe('simulateApply：依序模拟主流程', () => {
  it('正常依序替换并统计 counts', () => {
    const loc = { main: [['Alpha', '甲'], ['Beta', '乙']], renderer: [['Gamma', '丙']] };
    const r = simulateApply(loc, 'Alpha Beta', 'Gamma');
    expect(r.total).toBe(3);
    expect(r.counts).toEqual({ main: 2, renderer: 1, select: 0 });
    expect(r.hiddenCount).toBe(0);
  });

  it('识别隐性失效：前序条目改写后本条目 0 次替换，但独立检测仍命中（preempted）', () => {
    // 第一条把 'Edit' 改成 'Add'；第二条 'Edit model' 依序再也匹配不到，
    // 但对原始文本独立匹配 > 0 → 隐性失效（#206 类）
    const loc = { main: [['Edit', 'Add'], ['Edit model', '编辑模型']], renderer: [] };
    const r = simulateApply(loc, 'Edit model menu', '');
    expect(r.hidden.map((h) => h.reason)).toContain(SIM_REASON_PREEMPTED);
    const hidden = r.hidden.find((h) => h.pattern === 'Edit model');
    expect(hidden).toMatchObject({ array: 'main', index: 1, replaced: 0, origMatches: 1 });
  });

  it('识别三元素条目的门未命中（gate-miss）：第三参数对原文本命中、对依序文本不命中', () => {
    const loc = {
      main: [['Alpha-1 rest', 'Beta'], ['Alpha-1', '甲', 'Alpha-(\\d+)']],
      renderer: [],
    };
    const r = simulateApply(loc, 'Alpha-1 rest', '');
    expect(r.hidden.map((h) => h.reason)).toContain(SIM_REASON_GATE_MISS);
  });

  it('识别三元素条目回填后残留占位符（gate-leftover）', () => {
    const loc = {
      main: [['Alpha', 'Beta'], ['Alpha', '甲#{9}', '(\\d+)']],
      renderer: [],
    };
    const r = simulateApply(loc, 'Alpha-1', '');
    expect(r.hidden.map((h) => h.reason)).toContain(SIM_REASON_GATE_LEFTOVER);
  });

  it('select 段仅在 enable=true 且 replaceFile 匹配时参与', () => {
    const loc = {
      main: [], renderer: [],
      select: [
        { enable: false, replaceFile: 'main.js', replace: [['A', '甲']] },
        { enable: true, replaceFile: 'renderer.js', replace: [['B', '乙']] },
        { enable: true, replaceFile: 'main.js', replace: [['C', '丙']] },
      ],
    };
    const r = simulateApply(loc, 'C', 'B');
    expect(r.counts.select).toBe(2);
    expect(r.entries.map((e) => e.array)).toContain('select[2].replace');
    expect(r.entries.map((e) => e.array)).not.toContain('select[0].replace');
  });

  it('哨兵空串条目被跳过（不参与判定）', () => {
    const loc = { main: [['', '甲'], ['""', '乙'], ['Real Text', '真文本']], renderer: [] };
    const r = simulateApply(loc, 'Real Text', '');
    expect(r.hiddenCount).toBe(0);
    const real = r.entries.find((e) => e.array === 'main' && e.index === 2);
    expect(real.status).toBe('applied');
  });
});
