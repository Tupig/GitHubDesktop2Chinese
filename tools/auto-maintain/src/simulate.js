/**
 * 依序模拟检测（#207）。
 *
 * 现有失效检测（check-invalid.js）逐条独立测试正则是否匹配 js 文本，发现不了一类
 * 「隐性失效」：映射实际是依序应用的——前面的正则可能先把文本替换掉，导致后面的
 * 条目永不生效（例：renderer#883 "Add model" 先替换后，其后复合正则
 * "\"Edit model\":\"Add model\"" 匹配不到；独立测试时后者能匹配 → 失效检测报绿）。
 *
 * 本模块按 C++ 应用侧语义（TuPigGHD2Chinese.cpp 的 ProcessJsFile /
 * ApplySelectForFile / FillPlaceholders）对目标 js 文本依序应用
 * main / renderer / select(enable=true) 全部映射，逐条统计实际替换次数：
 *   - 本条替换次数 0，且该正则对原始（未替换）文本独立匹配 > 0 → 隐性失效（warning 级，
 *     供人工判断是否被抢占/死条目；输出与失效检测一致的「条目编号+正则+原因」格式）；
 *   - main_dev / renderer_dev 仅供 --devreplace 开发模式使用，不参与正常汉化流程，
 *     不在本模拟范围（与 #207 需求「依序应用 main/renderer/select 全部映射」一致）；
 *   - C++ 运行时的 ReDoS 防护跳过（#128）不在此复刻——CI 质量门已对命中嵌套量词的
 *     正则阻断，此类条目到不了依序应用阶段。
 */

// 与 C++ IsPlainLiteral 对齐: 无 ECMAScript 元字符的纯字面量走 string 快路径，
// 计数与替换结果和正则路径逐字节一致（ReplaceLiteralInPlace 契约）
const REGEX_METACHARS = '^$\\.*+?()[]{}|';

export function isPlainLiteral(pattern) {
  if (pattern === '') return false;
  for (const ch of pattern) {
    if (REGEX_METACHARS.includes(ch)) return false;
  }
  return true;
}

// 与 C++ EscapeDollar 对齐: 捕获文本中的 $ 转义为 $$，
// 防止回填值在后续正则替换的 format 串中被当作 $&/$n/$` 展开或吞掉
function escapeDollar(s) {
  return s.replace(/\$/g, '$$$$');
}

/**
 * 与 C++ FillPlaceholders 对齐的第三参数占位符回填：
 * 在 text 中查找 pattern3 的首个匹配，用捕获组逐字面回填 translation 中的 #{n}。
 * 返回 { status: 'filled' | 'miss' | 'leftover' | 'regex-error', translation }：
 *   - miss: 第三参数在当前文本匹配不到（条目将被 C++ 跳过）
 *   - leftover: 回填后仍残留 #{n}（捕获组不足，条目将被跳过）
 */
export function fillPlaceholders(text, pattern3, translation) {
  let re3;
  try {
    re3 = new RegExp(pattern3);
  } catch {
    return { status: 'regex-error', translation };
  }
  const m = re3.exec(text);
  if (!m) {
    return { status: 'miss', translation };
  }
  let filled = translation;
  for (let i = 1; i < m.length; i++) {
    if (m[i] === undefined) continue;
    const ph = '#{' + i + '}';
    const val = escapeDollar(m[i]);
    filled = filled.split(ph).join(val);
  }
  if (/#\{\d+\}/.test(filled)) {
    return { status: 'leftover', translation: filled };
  }
  return { status: 'filled', translation: filled };
}

/**
 * 统计 pattern 在 text 中的非重叠匹配数（对应 C++ sregex_iterator 遍历口径，
 * 与失效检测的独立 test 同一对象文本）。正则编译失败返回 null
 * （调用方按「无法独立匹配」处理，交由失效检测报 regex-error）。
 */
export function countMatches(text, pattern) {
  let re;
  try {
    re = new RegExp(pattern, 'g');
  } catch {
    return null;
  }
  let n = 0;
  for (const _ of text.matchAll(re)) n++;
  return n;
}

// 隐性失效原因标识（与 check-invalid 的 reason 风格一致，#207）：
//   preempted:      本条正则被前面条目抢占，依序替换 0 次（#206 的 "Edit model":"Add model" 类）
//   gate-miss:      三元素条目的第三参数在依序文本中匹配不到，整条被跳过
//   gate-leftover:  第三参数回填后仍残留 #{n} 占位符，整条被跳过
export const SIM_REASON_PREEMPTED = 'preempted';
export const SIM_REASON_GATE_MISS = 'gate-miss';
export const SIM_REASON_GATE_LEFTOVER = 'gate-leftover';

// 参与隐性失效判定的条目状态: 通过全部条件门执行了替换（replaced 可为 0），
// 或被三元素条件门拦截（gate-miss / gate-leftover）——两者均可能「独立检测报绿、
// 依序应用不生效」；结构性跳过与编译失败由失效检测负责，不重复报
const JUDGEABLE_STATUS = new Set(['applied', 'gate-miss', 'gate-leftover']);

/**
 * 对当前文本依序应用一个映射条目，返回 { text, record }。
 * 语义对齐 C++ 正常替换路径：
 *   - item[0] 为查找正则，空串或 '""' 跳过（C++ 同款哨兵）；
 *   - item[0] 编译失败记 regex-error（C++ 正常模式抛错中止，此处不中断模拟，
 *     由失效检测负责报告）；
 *   - 三元素条目先用 item[2] 对当前文本回填 item[1] 中的 #{n}，miss/leftover 整条跳过；
 *   - 纯字面量走 string 快路径，替换文本含 $ 时走正则 format 展开
 *     （与 C++ use_literal 判定一致，$ 检查在第三参数回填之后）；
 *   - 全部非重叠匹配一次替换，零命中不改动文本。
 */
function applyEntry(records, arrayName, index, item, text) {
  const record = {
    array: arrayName,
    index,
    pattern: null,
    before: 0,
    replaced: 0,
    status: 'applied',
    origMatches: null,
  };
  records.push(record);

  // 畸形条目（C++ E2 防御: 跳过不中断），独立匹配无从谈起，不构成隐性失效
  if (!Array.isArray(item) || item.length === 0 || typeof item[0] !== 'string') {
    record.status = 'skipped-invalid';
    return { text, record };
  }
  const rege = item[0];
  record.pattern = rege;
  if (rege === '' || rege === '""') {
    record.status = 'skipped-empty';
    return { text, record };
  }

  // 依序应用前的匹配数（应用后即被全部替换, 故 replaced === before）
  let before = 0;
  let re = null;
  if (isPlainLiteral(rege)) {
    let pos = 0;
    while ((pos = text.indexOf(rege, pos)) !== -1) {
      before++;
      pos += rege.length;
    }
  } else {
    try {
      re = new RegExp(rege, 'g');
    } catch {
      record.status = 'regex-error';
      return { text, record };
    }
    for (const _ of text.matchAll(re)) before++;
  }
  record.before = before;

  // 三元素条目: 第三参数条件门 + #{n} 占位符回填（未命中/残留均整条跳过, C++ 同语义）
  let translation = item[1];
  if (item.length >= 3 && typeof item[2] === 'string') {
    if (typeof translation !== 'string') {
      record.status = 'skipped-no-replacement';
      return { text, record };
    }
    const fill = fillPlaceholders(text, item[2], translation);
    if (fill.status !== 'filled') {
      record.status = fill.status === 'miss' ? 'gate-miss'
        : fill.status === 'leftover' ? 'gate-leftover' : 'gate-regex-error';
      return { text, record };
    }
    translation = fill.translation;
  } else if (typeof translation !== 'string') {
    // 缺少替换文本（C++ 跳过并告警），非隐性失效
    record.status = 'skipped-no-replacement';
    return { text, record };
  }

  // 零命中不改动文本（与 C++ 零命中不拷贝语义等价）
  if (before === 0) {
    return { text, record };
  }
  record.replaced = before;
  if (re === null && !translation.includes('$')) {
    // 字面量快路径: split/join 即非重叠全量字面替换
    return { text: text.split(rege).join(translation), record };
  }
  if (re === null) {
    // 回填后引入 $ 的字面量条目: 此刻补构造（字面量必为合法正则, 不抛）
    re = new RegExp(rege, 'g');
  }
  return { text: text.replace(re, translation), record };
}

/**
 * 隐性失效判定（#207 核心逻辑）：
 * 本条依序替换次数为 0，且查找正则对原始（未替换）文本独立匹配 > 0——
 * 即「失效检测独立测试报绿、依序应用却永不生效」的条目。
 * 三元素条目额外要求第三参数独立匹配 > 0（否则失效检测已将其报为 not-found，不重复报）。
 */
function judgeHidden(hidden, record, item, origText) {
  if (!JUDGEABLE_STATUS.has(record.status)) return;
  if (record.replaced > 0 || record.pattern === null) return;
  const orig0 = countMatches(origText, record.pattern);
  record.origMatches = orig0;
  if (!orig0) return;
  if (item.length >= 3 && typeof item[2] === 'string') {
    const orig2 = countMatches(origText, item[2]);
    if (!orig2) return;
    hidden.push({
      ...record,
      reason: record.status === 'gate-miss' ? SIM_REASON_GATE_MISS
        : record.status === 'gate-leftover' ? SIM_REASON_GATE_LEFTOVER
          : SIM_REASON_PREEMPTED,
    });
    return;
  }
  hidden.push({ ...record, reason: SIM_REASON_PREEMPTED });
}

/**
 * 主流程：依序应用 main/renderer/select 全部映射，逐条统计实际替换次数。
 * 返回 {
 *   total, hiddenCount,
 *   hidden:  [{ array, index, pattern, reason, before, replaced, origMatches }],
 *   entries: [{ array, index, pattern, before, replaced, status, origMatches }],  // 全量逐条计数
 *   counts:  { main, renderer, select },  // 各来源条目数
 * }
 */
export function simulateApply(localization, mainJsText, rendererJsText) {
  const records = [];
  const hidden = [];
  const counts = { main: 0, renderer: 0, select: 0 };

  // 单文件依序模拟: 基础数组 → select 段（enable=true 且 replaceFile 精确匹配，顺序与 C++ 一致）
  const simulateFile = (baseArrayName, baseArr, origText, fileTag) => {
    let text = origText;
    if (Array.isArray(baseArr)) {
      for (let i = 0; i < baseArr.length; i++) {
        const r = applyEntry(records, baseArrayName, i, baseArr[i], text);
        text = r.text;
        judgeHidden(hidden, r.record, baseArr[i], origText);
        counts[baseArrayName]++;
      }
    }
    const selects = localization.select;
    if (Array.isArray(selects)) {
      for (let s = 0; s < selects.length; s++) {
        const sel = selects[s];
        // 与 C++ 应用侧一致: 仅 enable=true 且 replaceFile 精确等于目标文件的 select 参与替换
        if (sel?.enable !== true || sel?.replaceFile !== fileTag) continue;
        const replaces = sel?.replace;
        if (!Array.isArray(replaces)) continue;
        for (let j = 0; j < replaces.length; j++) {
          const r = applyEntry(records, `select[${s}].replace`, j, replaces[j], text);
          text = r.text;
          judgeHidden(hidden, r.record, replaces[j], origText);
          counts.select++;
        }
      }
    }
    return text;
  };

  simulateFile('main', localization.main, mainJsText, 'main.js');
  simulateFile('renderer', localization.renderer, rendererJsText, 'renderer.js');

  return {
    total: records.length,
    hiddenCount: hidden.length,
    hidden,
    entries: records,
    counts,
  };
}
