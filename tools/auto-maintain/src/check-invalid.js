import fs from 'node:fs';

/**
 * 读取 localization.json，返回解析后的对象
 */
export function loadLocalization(jsonPath) {
  if (!fs.existsSync(jsonPath)) {
    throw new Error(`localization.json 不存在: ${jsonPath}`);
  }
  const raw = fs.readFileSync(jsonPath, 'utf8');
  try {
    return JSON.parse(raw);
  } catch (e) {
    // 报错带路径: 否则 CI/本地无法区分是哪份 json 损坏
    throw new Error(`localization.json 解析失败 (${jsonPath}): ${e.message}`);
  }
}

/**
 * std::regex(ECMAScript 语法子集) 不兼容语法黑名单。
 * JS new RegExp 接受但 C++ std::regex 会抛 regex_error(或行为不同)的写法:
 *  - (?<= / (?<!  后行断言(ECMAScript 2018+, std::regex 不支持)
 *  - (?<name>    命名分组
 *  - \p{...}     Unicode 属性转义
 *  - (?i) 等内联 flag
 * 若不显式拦截, 这些 pattern 会被 JS 判为 ok, 实际运行时 C++ 侧 regex_error 中断汉化。
 */
const STD_REGEX_BLACKLIST = [
  { re: /\(\?<=/, why: '后行断言 (?<=)' },
  { re: /\(\?<!/, why: '负后行断言 (?<!)' },
  { re: /\(\?<[A-Za-z_]/, why: '命名分组 (?<name>)' },
  { re: /\(\?P[<a-zA-Z]/, why: 'Python 命名分组 (?P<name>)' },
  { re: /\\p\{/, why: 'Unicode 属性转义 \\p{...}' },
  { re: /\(\?[a-zA-Z]+[):]/, why: '内联 flag (?i:...)' },
];

/**
 * 对单个映射项执行失效检测（与 C++ std::regex 语义对齐）。
 * 规则（对应 GitHubDesktop2Chinese.cpp 的 --invalidcheck）：
 *  - item[0] 为查找正则，为空跳过
 *  - item[2]（可选）为第三个参数正则，需额外匹配
 * 返回 { ok, errors: [{ reason, pattern }] }，
 * reason 为 'not-found' | 'regex-error' | 'unsupported-syntax'
 */
export function checkEntry(jsText, item) {
  const errors = [];
  const patterns = [];
  if (item && typeof item[0] === 'string') {
    patterns.push(item[0]);
  }
  if (item && item.length >= 3 && typeof item[2] === 'string') {
    patterns.push(item[2]);
  }

  for (const p of patterns) {
    if (!p || p === '""') continue;
    const hit = STD_REGEX_BLACKLIST.find(b => b.re.test(p));
    if (hit) {
      errors.push({ reason: `unsupported-syntax: ${hit.why}`, pattern: p });
      continue;
    }
    try {
      const re = new RegExp(p);
      if (!re.test(jsText)) {
        errors.push({ reason: 'not-found', pattern: p });
      }
    } catch {
      errors.push({ reason: 'regex-error', pattern: p });
    }
  }
  return { ok: errors.length === 0, errors };
}

/**
 * 遍历映射数组，对每个条目做失效检测。
 * 返回 { total, ok, failed, summary }，failed 为失效条目（含来源数组名）。
 */
export function checkInvalid(localization, mainJsText, rendererJsText) {
  const failed = [];
  let total = 0;
  let okCount = 0;

  const checkArray = (arrayName, jsText) => {
    const arr = localization[arrayName];
    if (!Array.isArray(arr)) return;
    for (let i = 0; i < arr.length; i++) {
      total++;
      const { ok, errors } = checkEntry(jsText, arr[i]);
      if (ok) {
        okCount++;
      } else {
        failed.push({
          array: arrayName,
          index: i,
          item: arr[i],
          errors,
        });
      }
    }
  };

  checkArray('main', mainJsText);
  checkArray('renderer', rendererJsText);
  checkArray('main_dev', mainJsText);
  checkArray('renderer_dev', rendererJsText);

  // 检测 select 中的替换项（对应 C++ 的 invalidcheck 对 select 的遍历）
  const selects = localization.select;
  if (Array.isArray(selects)) {
    for (let s = 0; s < selects.length; s++) {
      const sel = selects[s];
      // 与 C++ --invalidcheck 对齐: 仅检测 enable=true 的条目(被禁用的本就不参与替换, 允许保留过时内容)
      if (sel?.enable !== true) continue;
      const replaces = sel?.replace;
      if (!Array.isArray(replaces)) continue;
      // 与 C++ 应用侧一致: 仅识别 "main.js"/"renderer.js" 字面量(源码按精确相等判断),
      // 其它取值整条 select 不参与替换也不参与失效计数, 跳过
      const rf = sel?.replaceFile;
      if (rf !== 'main.js' && rf !== 'renderer.js') continue;
      const targetJs = rf === 'main.js' ? mainJsText : rendererJsText;
      for (let j = 0; j < replaces.length; j++) {
        total++;
        const { ok, errors } = checkEntry(targetJs, replaces[j]);
        if (ok) {
          okCount++;
        } else {
          failed.push({
            array: `select[${s}].replace`,
            index: j,
            item: replaces[j],
            errors,
          });
        }
      }
    }
  }

  return { total, ok: okCount, failed, failedCount: failed.length };
}