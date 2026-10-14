// CI 失败通知 Issue 的创建/更新/恢复关闭逻辑(由工作流 github-script 步骤 require 调用) #131
// 入参: { github, context, core } —— 与 actions/github-script 内置对象一致
// 触发范围: 仅 schedule 定时构建(push/PR 失败在 Actions/PR 页即时可见, 无需 Issue 渠道)
// 语义:
//   - 本次 run 有失败 job   → 创建/更新 "[CI 失败]" Issue(单条复用, 标题带连续失败次数)
//   - 本次 run 无失败 job   → 关闭历史 "[CI 失败]" Issue(评论恢复链接); 无历史则 no-op
// 计数: Issue body 内嵌 HTML 注释 <!-- ci-failure-count: N -->, 更新时解析 +1(对用户不可见)
module.exports = async ({ github, context, core }) => {
  try {
    const owner = context.repo.owner;
    const repo = context.repo.repo;

    // 本次 run 的失败 job 清单(conclusion === 'failure'; skipped/cancelled/success 均不计)
    const { data: jobs } = await github.rest.actions.listJobsForWorkflowRun({
      owner,
      repo,
      run_id: context.runId,
      per_page: 100,
    });
    const failedJobs = jobs.jobs.filter(j => j.conclusion === 'failure');

    // 列 open issue 按标题前缀识别(不按 label 过滤, 与 report-issue.js #144 同因:
    // label 被人工移除会漏查导致重复开单; 该前缀仅本脚本使用)
    const { data: issues } = await github.rest.issues.listForRepo({
      owner,
      repo,
      state: 'open',
      per_page: 100,
    });
    const existing = issues.find(i => i.title.startsWith('[CI 失败]'));

    const today = new Date().toISOString().slice(0, 10);
    const runUrl = `https://github.com/${owner}/${repo}/actions/runs/${context.runId}`;

    // ── 恢复分支: 无失败且存在历史失败 Issue → 关闭(评论附恢复 run 链接) ──
    if (failedJobs.length === 0) {
      if (existing) {
        await github.rest.issues.createComment({
          owner,
          repo,
          issue_number: existing.number,
          body: [
            `✅ 定时构建已恢复（${today}，[本次运行](${runUrl})全部通过）。`,
            '',
            '如有再次失败将重新开启追踪。',
          ].join('\n'),
        });
        await github.rest.issues.update({
          owner,
          repo,
          issue_number: existing.number,
          state: 'closed',
        });
        console.log(`CI 已恢复, 关闭 Issue #${existing.number}`);
      } else {
        console.log('本次运行无失败且无历史失败 Issue, 无需操作');
      }
      return;
    }

    // ── 失败分支: 解析历史计数 → 连续次数 +1 ──
    const countMatch = existing ? (existing.body || '').match(/<!-- ci-failure-count: (\d+) -->/) : null;
    const prevCount = countMatch ? Number(countMatch[1]) : 0;
    const count = prevCount + 1;

    const failedList = failedJobs.map(j => `- [ ] [${j.name}](${j.html_url})`).join('\n');
    const title = `[CI 失败] 定时构建连续失败 ${count} 次 (最新 ${today})`;
    const body = [
      `<!-- ci-failure-count: ${count} -->`,
      `## 定时构建失败报告（第 ${count} 次，${today}）`,
      '',
      `本 Issue 由 CI 失败通知任务自动维护（#131）：定时构建连续失败时更新本单，恢复后自动关闭。`,
      '',
      `**失败任务（本次 [运行记录](${runUrl})）：**`,
      '',
      failedList,
      '',
      '---',
      `**排查建议：**`,
      `- 打开上方失败任务链接查看日志末尾的错误输出`,
      `- 常见诱因：上游 GitHub Desktop 结构变更 / OpenSSL 源地址变动 / 网络抖动`,
      `- 修复后无需手动处理本 Issue：下次定时构建成功将自动关闭`,
      '',
      '---',
      `_由 [GitHubDesktop2Chinese](https://github.com/${owner}/${repo}/actions/workflows/ghdesktop2chinese.yml) CI 失败通知任务自动生成_`,
    ].join('\n');

    if (existing) {
      await github.rest.issues.update({ owner, repo, issue_number: existing.number, title, body });
      console.log(`CI 连续失败 ${count} 次, 已更新 Issue #${existing.number}`);
    } else {
      const created = await github.rest.issues.create({
        owner,
        repo,
        title,
        body,
        labels: ['ci-failure'],
      });
      console.log(`CI 首次失败, 已创建 Issue #${created.data.number}`);
    }
  } catch (err) {
    // 通知任务自身失败不应让整个 run 变红(会递归触发下一次的通知负担), 降级为 warning
    core.warning(`CI 失败通知失败: ${err.message}`);
  }
};
