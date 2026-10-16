// extract-new.js 单元测试（Vitest）
// 覆盖字符串字面量提取/转义/模式收集/覆盖判定/候选提取主流程（原仅在 tools-self-test 中浅覆盖）
import { describe, it, expect } from 'vitest';
import {
  isLikelyUiText, extractStringLiterals, escapeRegex, buildDraftLine,
  collectPatterns, isCoveredByPatterns, extractNew,
} from '../src/extract-new.js';

describe('extractStringLiterals：字符串字面量提取', () => {
  it('提取双引号/单引号候选并过滤非 UI 文本', () => {
    const js = `const a = "Checkout commit"; const b = 'into '; const c = "GitHub Desktop";`
      + ` const d = "rgba(255, 255, 255, 0.3)"; const e = "h:mm aaa";`;
    const texts = extractStringLiterals(js).map((x) => x.text);
    expect(texts).toContain('Checkout commit');
    expect(texts).toContain('GitHub Desktop');
    expect(texts).not.toContain('into ');                 // 首尾空白片段被过滤
    expect(texts).not.toContain('rgba(255, 255, 255, 0.3)'); // 内含圆括号被过滤
    expect(texts).not.toContain('h:mm aaa');              // 时间格式串被过滤
  });

  it('还原 \\" 转义为真实引号（与 JS 语义一致）', () => {
    const texts = extractStringLiterals('"Say \\"Hello\\" now"').map((x) => x.text);
    expect(texts).toContain('Say "Hello" now');
  });

  it('含换行（\\n 还原后）与反斜杠的字面量被过滤（非 UI 文案）', () => {
    const texts = extractStringLiterals('"Line one\\nLine two" "keep \\\\q here"').map((x) => x.text);
    expect(texts).toEqual([]);
  });
});

describe('escapeRegex / buildDraftLine：草稿生成', () => {
  it('escapeRegex 转义正则元字符', () => {
    expect(escapeRegex('a.b*c?')).toBe('a\\.b\\*c\\?');
    expect(escapeRegex('plain')).toBe('plain');
  });

  it('buildDraftLine 生成合法两元素 JSON 且按字面量匹配', () => {
    const line = buildDraftLine('Checkout commit?');
    const entry = JSON.parse('[' + line + ']')[0];
    expect(entry).toHaveLength(2);
    expect(entry[1]).toBe('"【待翻译】"');
    expect(new RegExp(entry[0]).test('"Checkout commit?"')).toBe(true);
  });
});

describe('collectPatterns / isCoveredByPatterns', () => {
  it('收集 main/renderer/main_dev/renderer_dev 与 enable=true 的 select 模式，剔除非法正则与哨兵', () => {
    const loc = {
      main: [['Hit', 'x'], ['', 'y'], ['"""', 'z'], ['(', 'bad-regex']],
      renderer: [['HitR', 'x', 'third']],
      select: [
        { enable: false, replace: [['Disabled', 'x']] },
        { enable: true, replace: [['EnabledSel', 'x']] },
      ],
    };
    const patterns = collectPatterns(loc);
    const asSource = patterns.map((re) => re.source);
    expect(asSource).toContain('Hit');
    expect(asSource).toContain('third');
    expect(asSource).toContain('EnabledSel');
    expect(asSource).not.toContain('Disabled');
    expect(asSource).not.toContain('""');
  });

  it('覆盖判定对裸文本/双引号/单引号三种形态任一命中即为已覆盖', () => {
    const patterns = collectPatterns({ main: [['"Checkout commit\\?"', 'x']] });
    expect(isCoveredByPatterns('Checkout commit?', patterns)).toBe(true);
    expect(isCoveredByPatterns('Never seen before', patterns)).toBe(false);
  });
});

describe('extractNew：候选提取主流程', () => {
  const loc = { main: [['"Checkout commit\\?"', '提交']], renderer: [] };

  it('去重计数、来源文件、按次数降序/长度升序、剔除已被覆盖项', () => {
    const mainJs = '"Checkout commit?" "New Alpha" "New Alpha"';
    const rendererJs = '"New Alpha" "New Beta Item"';
    const r = extractNew(loc, mainJs, rendererJs);
    expect(r.patternsCount).toBe(1);
    const byText = new Map(r.candidates.map((c) => [c.text, c]));
    expect(byText.has('Checkout commit?')).toBe(false); // 已被现有模式覆盖
    expect(byText.get('New Alpha')).toMatchObject({ count: 3 });
    expect(byText.get('New Alpha').files.sort()).toEqual(['main.js', 'renderer.js']);
    expect(r.candidates[0].text).toBe('New Alpha'); // 次数最高排最前
  });
});
