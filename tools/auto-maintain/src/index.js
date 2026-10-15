import path from 'node:path';
import fs from 'node:fs';
import os from 'node:os';
import { parseArgs, help } from './args.js';
import { fetchLatest } from './fetch.js';
import { loadLocalization, checkInvalid } from './check-invalid.js';
import { simulateApply, SIM_REASON_PREEMPTED, SIM_REASON_GATE_MISS, SIM_REASON_GATE_LEFTOVER } from './simulate.js';
import { extractNew } from './extract-new.js';
import { renderMarkdown } from './report.js';

let colorEnabled = true;
const c = (code, s) => (colorEnabled ? `\x1b[${code}m${s}\x1b[0m` : String(s));

async function getJsFiles(args) {
  if (!args.workdir) {
    args.workdir = path.join(os.tmpdir(), 'ghdesktop-auto-maintain');
    fs.mkdirSync(args.workdir, { recursive: true });
  }
  const mainJsPath = path.join(args.workdir, 'main.js');
  const rendererJsPath = path.join(args.workdir, 'renderer.js');

  if (args.keepJs && fs.existsSync(mainJsPath) && fs.existsSync(rendererJsPath)) {
    return { mainJsPath, rendererJsPath, workdir: args.workdir, version: 'local' };
  }

  console.log(c('36', '↻ 获取最新 GitHub Desktop...'));
  const release = await fetchLatest(args.workdir);

  // 将 JS 复制到工作目录顶层，便于 --keep-js 复用
  fs.copyFileSync(release.mainJsPath, mainJsPath);
  fs.copyFileSync(release.rendererJsPath, rendererJsPath);
  console.log(c('32', `✓ 已获取 GitHub Desktop ${release.version} (${release.tag})`));
  console.log(`  main.js     -> ${mainJsPath}`);
  console.log(`  renderer.js -> ${rendererJsPath}`);
  return { mainJsPath, rendererJsPath, workdir: args.workdir, version: release.version };
}

function writeReport(file, content) {
  fs.writeFileSync(file, content, 'utf8');
}

async function main() {
  let args;
  try {
    args = parseArgs(process.argv.slice(2));
    colorEnabled = !args.noColor;
  } catch (e) {
    console.error(c('31', '✗ ' + e.message));
    console.log(help);
    process.exit(2);
  }

  try {
    const localization = loadLocalization(args.json);
    const { mainJsPath, rendererJsPath, workdir, version } = await getJsFiles(args);
    const mainJsText = fs.readFileSync(mainJsPath, 'utf8');
    const rendererJsText = fs.readFileSync(rendererJsPath, 'utf8');

    const report = { version, generatedAt: new Date().toISOString(), checks: null, candidates: null, simulation: null };

    if (args.cmds.includes('check') || args.cmds.includes('all')) {
      const result = checkInvalid(localization, mainJsText, rendererJsText);
      report.checks = result;
      console.log('');
      console.log(c('1;33', '══════ 失效检测结果 ══════'));
      console.log(`  总映射项: ${result.total}`);
      console.log(c('32', `  有效项:   ${result.ok}`));
      console.log(c(result.failedCount > 0 ? '31' : '32', `  失效项:   ${result.failedCount}`));
      for (const f of result.failed) {
        // unsupported-syntax 显示原始原因(含具体语法说明), 不再笼统归为"匹配不到"
        const reason = f.errors.map(e => (
          e.reason === 'regex-error' ? '正则错误'
            : e.reason.startsWith('unsupported-syntax') ? e.reason
              : '匹配不到'
        )).join('; ');
        console.log(`  ${c('31', `[${f.array}#${f.index}]`)} ${reason}: ${f.errors[0].pattern}`);
      }
      if (result.warnings.length > 0) {
        console.log(c('1;33', `  告警项(不阻断): ${result.warnings.length}`));
        for (const w of result.warnings) {
          console.log(`  ${c('33', `[${w.array}#${w.index}]`)} ${w.reason}: ${w.pattern}`);
        }
      }
    }

    if (args.cmds.includes('simulate')) {
      // 依序模拟检测(#207): 依序应用 main/renderer/select 全部映射, 发现被前面正则抢占的隐性失效
      const result = simulateApply(localization, mainJsText, rendererJsText);
      report.simulation = result;
      // 隐性失效原因的中文呈现(与 check 块 reason 映射风格一致)
      const SIM_REASON_ZH = {
        [SIM_REASON_PREEMPTED]: '被前面正则抢占',
        [SIM_REASON_GATE_MISS]: '条件正则依序失配',
        [SIM_REASON_GATE_LEFTOVER]: '占位符未回填',
      };
      console.log('');
      console.log(c('1;33', '══════ 依序模拟检测结果 ══════'));
      console.log(`  模拟条目: ${result.total} (main ${result.counts.main}, renderer ${result.counts.renderer}, select ${result.counts.select})`);
      // 隐性失效为 warning 级(供人工判断), 不改变退出码
      console.log(c(result.hiddenCount > 0 ? '33' : '32', `  隐性失效: ${result.hiddenCount}`));
      for (const h of result.hidden) {
        console.log(`  ${c('33', `[${h.array}#${h.index}]`)} 隐性失效(${SIM_REASON_ZH[h.reason] ?? h.reason}): ${h.pattern}`);
      }
      if (args.verbose) {
        console.log(c('1;33', `  ── 全量逐条替换计数 (共 ${result.entries.length} 条) ──`));
        for (const e of result.entries) {
          // 替换 0 次的条目附带对原始文本的独立匹配数, 便于区分隐性失效与彻底失效
          const extra = e.replaced === 0 && e.origMatches !== null ? `(独立匹配 ${e.origMatches} 处)` : '';
          console.log(`  [${e.array}#${e.index}] 替换 ${e.replaced} 处${extra ? ' ' + extra : ''}: ${e.pattern ?? '(无效条目)'}`);
        }
      }
    }

    if (args.cmds.includes('extract') || args.cmds.includes('all')) {
      const result = extractNew(localization, mainJsText, rendererJsText);
      report.candidates = result;
      console.log('');
      console.log(c('1;33', `══════ 未翻译候选 (共 ${result.candidates.length} 条, 展示前 ${args.top} 条) ══════`));
      console.log(`  已加载映射正则: ${result.patternsCount} 条`);
      for (const cand of result.candidates.slice(0, args.top)) {
        console.log(`  ${c('36', `[×${cand.count}]`)} ${cand.text}`);
      }
    }

    if (args.writeReport) {
      const file = path.join(workdir, 'report.md');
      writeReport(file, renderMarkdown(report));
      console.log('');
      console.log(c('32', `✓ 报告已写入: ${file}`));
    }
  } catch (e) {
    console.error(c('31', '✗ ' + (e.stack || e.message)));
    process.exit(1);
  }
}

// 顶层兜底: main 内部已有 try/catch, 此处仅防御未来漏网异常以 unhandled rejection 形式暴露(#119)
main().catch((e) => {
  console.error(e);
  process.exit(1);
});