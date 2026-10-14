// 自动维护报告 Issue 的创建/更新逻辑(由工作流 github-script 步骤 require 调用)
// 入参: { github, context, core } —— 与 actions/github-script 内置对象一致

// #137: 从报告正文提取失效数(纯函数, tools-self-test 回归覆盖):
// 1) 优先解析 report.js renderMarkdown 输出的结构化 meta 行(<!-- stats: total=.. ok=.. failed=.. -->),
//    与中文展示文案解耦, 文案微调不影响解析;
// 2) 后备兼容旧报告/手动报告的中文统计行;
// 3) 都未命中返回 '?': Number('?')===0 为 false → noFailures 恒 false, 保守不自动关单(仅 core.warning)
function extractFailedCount(report) {
  const meta = report.match(/<!--\s*stats:\s*total=(\d+)\s+ok=(\d+)\s+failed=(\d+)\s*-->/);
  if (meta) return { failed: meta[3], source: 'meta' };
  const legacy = report.match(/总映射项\s+(\d+)，有效\s+(\d+)，失效\s+(\d+)。/);
  if (legacy) return { failed: legacy[3], source: 'legacy-text' };
  return { failed: '?', source: 'none' };
}

module.exports = async ({ github, context, core }) => {
  try {
    const fs = require('fs');
    const path = require('path');
    const reportPath = path.join(process.env.RUNNER_TEMP, 'ghdesktop-auto-maintain', 'report.md');
    const report = fs.readFileSync(reportPath, 'utf8');

    const { failed, source } = extractFailedCount(report);
    if (source === 'none') {
      // 解析失败若静默降级, 自动关单/标题计数会永久失效且无告警, 故显式 warning
      core.warning('report.md 中未找到 stats meta 行或中文统计行, 失效数按 ? 处理, 请检查 report.js 是否变更输出格式');
    }
    const noFailures = Number(failed) === 0;

    const title = `[自动维护] 汉化映射失效 ${failed} 项 / 未翻译候选 (GitHub Desktop 报告)`;
    const today = new Date().toISOString().slice(0, 10);
    const body = [
      `## 自动维护报告（${today}）`,
      '',
      '本 Issue 由自动维护工作流生成，用于提醒维护者关注汉化映射的健康度。',
      '',
      '> ⚠️ 此报告不直接修改 `localization.json`，请人工确认后处理。',
      '',
      '---',
      '',
      report,
      '',
      '---',
      `_由 [GitHubDesktop2Chinese](https://github.com/${context.repo.owner}/${context.repo.repo}/actions/workflows/ghdesktop2chinese.yml) 自动生成_`,
    ].join('\n');

    // 正文超长防护: GitHub Issue body 上限 65536 字符, 超限会导致创建/更新失败(恰在最需要报告时丢失)
    // 截断按 code unit 计数(与 API 限制同口径), 但切点落在 UTF-16 代理对中间会产生孤立代理、
    // 致 GitHub API 422 拒绝(#143), 故切点为高位代理时回退一位按码点边界截断
    const MAX_BODY = 60000;
    let finalBody = body;
    if (body.length > MAX_BODY) {
      const lastUnit = body.charCodeAt(MAX_BODY - 1);
      const cut = (lastUnit >= 0xD800 && lastUnit <= 0xDBFF) ? MAX_BODY - 1 : MAX_BODY;
      finalBody = body.slice(0, cut) + '\n\n> ⚠️ 报告过长，已截断；完整内容见工作流日志。\n';
    }

    // 不按 label 过滤(#144): 旧单 label 被人工移除后, labels 过滤会漏查导致重复开单;
    // 改为列出 open issue 按标题前缀识别(该前缀仅本脚本使用, 误匹配面可控)
    const { data: issues } = await github.rest.issues.listForRepo({
      owner: context.repo.owner,
      repo: context.repo.repo,
      state: 'open',
      per_page: 100,
    });

    const existing = issues.find(i => i.title.startsWith('[自动维护]'));

    if (noFailures) {
      if (existing) {
        await github.rest.issues.update({
          owner: context.repo.owner,
          repo: context.repo.repo,
          issue_number: existing.number,
          state: 'closed',
          body: finalBody,
        });
        console.log(`已关闭 Issue #${existing.number}（无失效项）`);
      } else {
        console.log('无失效项且无历史 Issue，无需操作');
      }
      return;
    }

    if (existing) {
      await github.rest.issues.update({
        owner: context.repo.owner,
        repo: context.repo.repo,
        issue_number: existing.number,
        title: title,
        body: finalBody,
      });
      console.log(`已更新 Issue #${existing.number}`);
    } else {
      const created = await github.rest.issues.create({
        owner: context.repo.owner,
        repo: context.repo.repo,
        title: title,
        body: finalBody,
        labels: ['auto-maintain'],
      });
      console.log(`已创建 Issue #${created.data.number}`);
    }
  } catch (err) {
    core.setFailed(`创建或更新自动维护 Issue 失败: ${err.message}`);
  }
};

// #137: 纯函数单独导出供 tools-self-test 跨文件契约回归
module.exports.extractFailedCount = extractFailedCount;
