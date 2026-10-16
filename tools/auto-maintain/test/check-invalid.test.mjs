// check-invalid.js 单元测试（Vitest）
// 覆盖 checkEntry 各失败/告警分支与 checkInvalid 的数组/select 遍历与过滤语义
import { describe, it, expect } from 'vitest';
import { checkEntry, checkInvalid, WARN_REASON_REDOS_SUSPECT } from '../src/check-invalid.js';

describe('checkEntry：单条映射失效检测', () => {
  it('命中 → ok=true 且无 error', () => {
    const r = checkEntry('"Pull origin"', ['Pull', '拉取']);
    expect(r.ok).toBe(true);
    expect(r.errors).toEqual([]);
  });

  it('未命中 → not-found', () => {
    const r = checkEntry('"Pull origin"', ['Absent', '缺失']);
    expect(r.ok).toBe(false);
    expect(r.errors).toEqual([{ reason: 'not-found', pattern: 'Absent' }]);
  });

  it('非法正则 → regex-error', () => {
    const r = checkEntry('any', ['(', '坏']);
    expect(r.ok).toBe(false);
    expect(r.errors[0].reason).toBe('regex-error');
  });

  it('std::regex 不兼容语法 → unsupported-syntax（后行断言）', () => {
    const r = checkEntry('abc', ['(?<=a)b', '后行断言']);
    expect(r.ok).toBe(false);
    expect(r.errors[0].reason).toMatch(/^unsupported-syntax: /);
  });

  it('ReDoS 可疑且命中 → ok=true 但保留告警（仅告警不阻断）', () => {
    const r = checkEntry('aaaa', ['(a+)+b?', '可疑']);
    expect(r.ok).toBe(true);
    expect(r.errors).toEqual([]);
    expect(r.warnings).toEqual([{ reason: WARN_REASON_REDOS_SUSPECT, pattern: '(a+)+b?' }]);
  });

  it('空 pattern 与哨兵 "" 跳过', () => {
    expect(checkEntry('x', ['', '空']).ok).toBe(true);
    expect(checkEntry('x', ['""', '哨兵']).ok).toBe(true);
    expect(checkEntry('x', ['', '空']).errors).toEqual([]);
  });

  it('三元素条目：item[2] 参与额外校验', () => {
    const hit = checkEntry('id:"about" click(){shell}', ['id:"about"', '替换文本', 'click\\(\\)\\{']);
    expect(hit.ok).toBe(true);
    expect(hit.errors).toEqual([]);
    const miss = checkEntry('id:"about"', ['id:"about"', '替换文本', 'never-matches-xyz']);
    expect(miss.ok).toBe(false);
    expect(miss.errors).toEqual([{ reason: 'not-found', pattern: 'never-matches-xyz' }]);
  });

  it('畸形条目：非字符串 item[0]/item[2] 不参与校验', () => {
    expect(checkEntry('x', [123, 'y']).errors).toEqual([]);        // item[0] 非字符串 → 无模式
    const r = checkEntry('a', ['a', 'b', 42]);                     // item[2] 非字符串 → 忽略
    expect(r).toEqual({ ok: true, errors: [], warnings: [] });
    expect(checkEntry('x', null).errors).toEqual([]);
  });
});

describe('checkInvalid：映射数组与 select 遍历', () => {
  const loc = {
    main: [['Hit', '命中'], ['AbsentMain', '缺']],
    renderer: [['HitR', '命中']],
    main_dev: [],
    renderer_dev: [['AbsentDev', '缺']],
  };

  it('统计 total/ok/failedCount 并带来源数组名', () => {
    const r = checkInvalid(loc, 'Hit', 'HitR');
    expect(r.total).toBe(4);
    expect(r.ok).toBe(2);
    expect(r.failedCount).toBe(2);
    expect(r.failed.map((f) => f.array).sort()).toEqual(['main', 'renderer_dev']);
  });

  it('非数组字段被忽略（不抛错）', () => {
    const r = checkInvalid({ main: 'not-an-array', select: 'nope' }, 'x', 'y');
    expect(r.total).toBe(0);
    expect(r.failedCount).toBe(0);
  });

  it('select：仅 enable=true 且 replaceFile 为 main.js/renderer.js 才计数', () => {
    const withSelect = {
      main: [],
      renderer: [],
      select: [
        { enable: false, replaceFile: 'main.js', replace: [['AbsentA', 'x']] },   // 禁用 → 跳过
        { enable: true, replaceFile: 'other.js', replace: [['AbsentB', 'x']] },   // 文件不符 → 跳过
        { enable: true, replaceFile: 'main.js', replace: [['AbsentC', 'x'], ['HitMain', 'y']] },
        { enable: true, replaceFile: 'renderer.js', replace: [['HitRenderer', 'y']] },
        { enable: true, replaceFile: 'main.js', replace: 'not-array' },           // 非数组 → 跳过
      ],
    };
    const r = checkInvalid(withSelect, 'HitMain', 'HitRenderer');
    expect(r.total).toBe(3);
    expect(r.ok).toBe(2);
    expect(r.failedCount).toBe(1);
    expect(r.failed[0].array).toBe('select[2].replace');
    expect(r.failed[0].index).toBe(0);
  });

  it('select 告警带上数组名与序号', () => {
    const withWarn = {
      select: [{ enable: true, replaceFile: 'main.js', replace: [['(a+)+b?', 'x']] }],
    };
    const r = checkInvalid(withWarn, 'aaaa', '');
    expect(r.warnings[0]).toMatchObject({ array: 'select[0].replace', index: 0, reason: WARN_REASON_REDOS_SUSPECT });
  });
});
