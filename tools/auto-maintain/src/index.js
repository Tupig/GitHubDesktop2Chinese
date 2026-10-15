import path from 'node:path';
import fs from 'node:fs';
import os from 'node:os';
import { parseArgs, help } from './args.js';
import { fetchLatest } from './fetch.js';
import { loadLocalization, checkInvalid } from './check-invalid.js';
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

    const report = { version, generatedAt: new Date().toISOString(), checks: null, candidates: null };

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