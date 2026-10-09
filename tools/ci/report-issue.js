// 自动维护报告 Issue 的创建/更新逻辑(由工作流 github-script 步骤 require 调用)
// 入参: { github, context, core } —— 与 actions/github-script 内置对象一致
module.exports = async ({ github, context, core }) => {
  try {
    const fs = require('fs');
    const path = require('path');
    const reportPath = path.join(process.env.RUNNER_TEMP, 'ghdesktop-auto-maintain', 'report.md');
    const report = fs.readFileSync(reportPath, 'utf8');

    const failedMatch = report.match(/总映射项\s+(\d+)，有效\s+(\d+)，失效\s+(\d+)。/);
    if (!failedMatch) {
      // 报告文案与本正则强耦合: 解析失败时若静默降级, 自动关单/标题计数会永久失效且无告警
      core.warning('report.md 中未找到失效统计行(总映射项 x，有效 x，失效 x。), 失效数按 ? 处理, 请检查 report.js 文案是否变更');
    }
    const failed = failedMatch ? failedMatch[3] : '?';
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
    const MAX_BODY = 60000;
    const finalBody = body.length > MAX_BODY
      ? body.slice(0, MAX_BODY) + '\n\n> ⚠️ 报告过长，已截断；完整内容见工作流日志。\n'
      : body;

    const { data: issues } = await github.rest.issues.listForRepo({
      owner: context.repo.owner,
      repo: context.repo.repo,
      state: 'open',
      labels: ['auto-maintain'],
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
