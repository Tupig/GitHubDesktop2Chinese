// CLI 参数解析(#174 从 index.js 抽出为无副作用纯模块, tools/ci/tools-self-test.mjs 覆盖各分支)
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PROJECT_ROOT = path.resolve(__dirname, '..', '..', '..');
const DEFAULT_JSON = path.join(PROJECT_ROOT, 'json', 'localization.json');

export const help = `用法:
  node src/index.js <check|simulate|extract|all> [选项]

子命令:
  check    对最新 GitHub Desktop 的 main.js/renderer.js 检测 localization.json 中的失效映射项
  simulate 依序模拟检测：按 C++ 应用顺序（main/renderer/select）依序应用全部映射，
           逐条统计实际替换次数，发现「被前面正则抢占」的隐性失效（#207）
  extract  提取最新 GitHub Desktop 中尚未被现有映射覆盖的英文 UI 文案候选
  all      check + extract

选项:
  --json <path>      localization.json 路径（默认 ${DEFAULT_JSON}）
  --workdir <dir>    下载/解压工作目录（默认系统临时目录 ghdesktop-auto-maintain）
  --keep-js          复用已有 workdir 中的 JS 文件（跳过下载，用于本地调试）
  --no-color         关闭输出颜色
  --top <n>          extract 最多输出的候选条数（默认 60）
  --verbose          simulate 输出全量逐条替换计数（默认仅输出隐性失效项）
  --write-report     将结果写入 workdir 下的 report.md
  -h, --help         显示帮助
`;

/**
 * 解析命令行参数。非法输入（未知参数 / 选项缺值 / --top 非非负整数）抛错，由调用方呈现。
 */
export function parseArgs(argv) {
  const args = { json: DEFAULT_JSON, top: 60, writeReport: false, keepJs: false, noColor: false, verbose: false, cmds: [] };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--json') {
      const v = argv[++i];
      if (v === undefined) throw new Error('--json 需要一个路径值');
      args.json = v;
    }
    else if (a === '--workdir') {
      const v = argv[++i];
      if (v === undefined) throw new Error('--workdir 需要一个目录值');
      args.workdir = v;
    }
    else if (a === '--top') {
      const v = parseInt(argv[++i], 10);
      if (!Number.isFinite(v) || v < 0) throw new Error(`--top 需要一个非负整数, 收到: ${argv[i]}`);
      args.top = v;
    }
    else if (a === '--write-report') args.writeReport = true;
    else if (a === '--keep-js') args.keepJs = true;
    else if (a === '--no-color') args.noColor = true;
    else if (a === '--verbose') args.verbose = true;
    else if (a === '-h' || a === '--help') { console.log(help); process.exit(0); }
    else if (['check', 'simulate', 'extract', 'all'].includes(a)) args.cmds.push(a);
    else throw new Error(`未知参数: ${a}`);
  }
  if (args.cmds.length === 0) args.cmds = ['all'];
  return args;
}
