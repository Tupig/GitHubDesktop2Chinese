// tools/ci/report-issue.js 与 ci-failure-notify.js 的处理器级单元测试（Vitest）
// 以注入的假 octokit（github/context/core）驱动 handler，覆盖：
//   - 分页聚合回归（#230 目标单漏检 / #247 失败 job 漏检）——须走 github.paginate
//   - 开单 / 更新 / 关单（恢复）分支与标题同步
//   - 正文超长截断的 UTF-16 代理对边界安全（#143）
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import fs from 'node:fs';
import os from 'node:os';
import nodePath from 'node:path';
import { createRequire } from 'node:module';

const requireCjs = createRequire(import.meta.url);
const reportIssue = requireCjs('../../ci/report-issue.js');
const ciNotify = requireCjs('../../ci/ci-failure-notify.js');

const OWNER = 'o';
const REPO = 'r';
let runnerTemp;

beforeEach(() => {
  runnerTemp = fs.mkdtempSync(nodePath.join(os.tmpdir(), 'ghd2c-runner-'));
  fs.mkdirSync(nodePath.join(runnerTemp, 'ghdesktop-auto-maintain'), { recursive: true });
  process.env.RUNNER_TEMP = runnerTemp;
});
afterEach(() => {
  delete process.env.RUNNER_TEMP;
  fs.rmSync(runnerTemp, { recursive: true, force: true });
});

function writeReport(text) {
  fs.writeFileSync(
    nodePath.join(runnerTemp, 'ghdesktop-auto-maintain', 'report.md'),
    text, 'utf8');
}

/** 假 octokit：以身份稳定的函数引用区分两处 paginate 目标，并记录全部写调用 */
function makeGithub({ issues = [], jobs = [] } = {}) {
  const listForRepo = function listForRepo() {};
  const listJobs = function listJobsForWorkflowRun() {};
  const update = vi.fn(async () => ({ data: {} }));
  const create = vi.fn(async () => ({ data: { number: 42 } }));
  const createComment = vi.fn(async () => ({ data: {} }));
  const paginate = vi.fn(async (fn) => {
    if (fn === listForRepo) return issues;
    if (fn === listJobs) return jobs;
    return [];
  });
  const github = {
    paginate,
    rest: {
      issues: { listForRepo, update, create, createComment },
      actions: { listJobsForWorkflowRun: listJobs },
    },
  };
  return { github, update, create, createComment, paginate, listForRepo, listJobs };
}

function makeCtx() {
  return { repo: { owner: OWNER, repo: REPO } };
}
function makeCore() {
  return { setFailed: vi.fn(), warning: vi.fn() };
}

const META_REPORT = '## 自动维护报告\n\n总映射项 1119，有效 1117，失效 2。\n<!-- stats: total=1119 ok=1117 failed=2 -->\n';
const HEALTHY_REPORT = '## 自动维护报告\n\n总映射项 1119，有效 1119，失效 0。\n<!-- stats: total=1119 ok=1119 failed=0 -->\n';

describe('report-issue.js：自动维护报告单', () => {
  it('有失效且无历史单 → 创建（标题含失效数 / label auto-maintain）', async () => {
    writeReport(META_REPORT);
    const { github, create, update, paginate } = makeGithub({ issues: [] });
    const core = makeCore();

    await reportIssue({ github, context: makeCtx(), core });

    expect(paginate).toHaveBeenCalledWith(github.rest.issues.listForRepo, expect.objectContaining({ state: 'open', per_page: 100 }));
    expect(update).not.toHaveBeenCalled();
    expect(create).toHaveBeenCalledTimes(1);
    const arg = create.mock.calls[0][0];
    expect(arg.title).toBe('[自动维护] 汉化映射失效 2 项 / 未翻译候选 (GitHub Desktop 报告)');
    expect(arg.labels).toEqual(['auto-maintain']);
    expect(arg.body).toContain('<!-- stats: total=1119 ok=1117 failed=2 -->');
    expect(core.setFailed).not.toHaveBeenCalled();
  });

  it('有失效且有历史单 → 更新（不新建）', async () => {
    writeReport(META_REPORT);
    const { github, create, update } = makeGithub({ issues: [{ number: 7, title: '[自动维护] 汉化映射失效 5 项 / 未翻译候选 (GitHub Desktop 报告)' }] });

    await reportIssue({ github, context: makeCtx(), core: makeCore() });

    expect(create).not.toHaveBeenCalled();
    expect(update).toHaveBeenCalledTimes(1);
    const arg = update.mock.calls[0][0];
    expect(arg.issue_number).toBe(7);
    expect(arg.title).toBe('[自动维护] 汉化映射失效 2 项 / 未翻译候选 (GitHub Desktop 报告)');
    expect(arg.state).toBeUndefined();
  });

  it('无失效且有历史单 → 关闭并同步标题（已恢复）', async () => {
    writeReport(HEALTHY_REPORT);
    const { github, create, update } = makeGithub({ issues: [{ number: 9, title: '[自动维护] 汉化映射失效 4 项 / 未翻译候选 (GitHub Desktop 报告)' }] });

    await reportIssue({ github, context: makeCtx(), core: makeCore() });

    expect(create).not.toHaveBeenCalled();
    const arg = update.mock.calls[0][0];
    expect(arg.issue_number).toBe(9);
    expect(arg.state).toBe('closed');
    expect(arg.title).toContain('（已恢复）');
  });

  it('无失效且无历史单 → 无写操作', async () => {
    writeReport(HEALTHY_REPORT);
    const { github, create, update } = makeGithub({ issues: [] });

    await reportIssue({ github, context: makeCtx(), core: makeCore() });

    expect(create).not.toHaveBeenCalled();
    expect(update).not.toHaveBeenCalled();
  });

  it('统计行缺失 → warning 且按 ? 处理（仍存在历史单时只更新不关单）', async () => {
    writeReport('## 自动维护报告\n\n（无统计信息）\n');
    const { github, update } = makeGithub({ issues: [{ number: 11, title: '[自动维护] 汉化映射失效 3 项 / 未翻译候选 (GitHub Desktop 报告)' }] });
    const core = makeCore();

    await reportIssue({ github, context: makeCtx(), core });

    expect(core.warning).toHaveBeenCalledTimes(1);
    expect(update).toHaveBeenCalledTimes(1);
    expect(update.mock.calls[0][0].state).toBeUndefined(); // 保守: 不自动关单
  });

  it('正文超长 → 截断且不在 UTF-16 代理对中间断开（#143）', async () => {
    // 大量代理对（每字符 2 个 code unit）确保 60000 处可能落在代理对中间
    writeReport('😀'.repeat(40000));
    const { github, create } = makeGithub({ issues: [] });

    await reportIssue({ github, context: makeCtx(), core: makeCore() });

    const body = create.mock.calls[0][0].body;
    expect(body.endsWith('> ⚠️ 报告过长，已截断；完整内容见工作流日志。\n')).toBe(true);
    // 注意: 报告头部另有「不直接修改 localization.json」的 ⚠️ 行, 故按截断标记文本定位
    const prefix = body.slice(0, body.indexOf('\n\n> ⚠️ 报告过长'));
    expect([59999, 60000]).toContain(prefix.length);
    const lastUnit = prefix.charCodeAt(prefix.length - 1);
    expect(lastUnit >= 0xD800 && lastUnit <= 0xDBFF).toBe(false); // 不得以孤立高位代理结尾
    expect(() => JSON.stringify(body)).not.toThrow();
  });

  it('读取报告失败 → setFailed（不抛出）', async () => {
    // 未写入 report.md
    const { github } = makeGithub({ issues: [] });
    const core = makeCore();

    await expect(reportIssue({ github, context: makeCtx(), core })).resolves.toBeUndefined();
    expect(core.setFailed).toHaveBeenCalledTimes(1);
  });
});

describe('ci-failure-notify.js：定时构建失败单', () => {
  const ctx = () => ({ repo: { owner: OWNER, repo: REPO }, runId: 12345 });

  it('首次失败 → 创建（计数 1 / label ci-failure）', async () => {
    const { github, create, listForRepo, listJobs } = makeGithub({
      issues: [],
      jobs: [{ name: '构建 Linux_x64', conclusion: 'failure', html_url: 'u1' }],
    });

    await ciNotify({ github, context: ctx(), core: makeCore() });

    expect(github.paginate).toHaveBeenCalledWith(listJobs, expect.objectContaining({ run_id: 12345 }));
    expect(github.paginate).toHaveBeenCalledWith(listForRepo, expect.objectContaining({ state: 'open' }));
    const arg = create.mock.calls[0][0];
    expect(arg.title).toMatch(/^\[CI 失败\] 定时构建连续失败 1 次 \(最新 \d{4}-\d{2}-\d{2}\)$/);
    expect(arg.body).toContain('<!-- ci-failure-count: 1 -->');
    expect(arg.body).toContain('[构建 Linux_x64](u1)');
    expect(arg.labels).toEqual(['ci-failure']);
  });

  it('再次失败 → 计数自历史上单递增并更新', async () => {
    const { github, create, update } = makeGithub({
      issues: [{ number: 3, title: '[CI 失败] 定时构建连续失败 2 次 (最新 2026-01-01)', body: '<!-- ci-failure-count: 2 -->' }],
      jobs: [{ name: 'Build', conclusion: 'failure', html_url: 'u' }],
    });

    await ciNotify({ github, context: ctx(), core: makeCore() });

    expect(create).not.toHaveBeenCalled();
    expect(update).toHaveBeenCalledTimes(1);
    const arg = update.mock.calls[0][0];
    expect(arg.issue_number).toBe(3);
    expect(arg.title).toMatch(/连续失败 3 次/);
    expect(arg.body).toContain('<!-- ci-failure-count: 3 -->');
  });

  it('本次全部成功且有历史单 → 评论 + 关单并同步标题', async () => {
    const { github, update, createComment } = makeGithub({
      issues: [{ number: 5, title: '[CI 失败] 定时构建连续失败 4 次 (最新 2026-01-02)', body: '<!-- ci-failure-count: 4 -->' }],
      jobs: [{ name: 'Build', conclusion: 'success', html_url: 'u' }],
    });

    await ciNotify({ github, context: ctx(), core: makeCore() });

    expect(createComment).toHaveBeenCalledTimes(1);
    expect(createComment.mock.calls[0][0].issue_number).toBe(5);
    expect(update.mock.calls[0][0].state).toBe('closed');
    expect(update.mock.calls[0][0].title).toContain('（已恢复）');
  });

  it('本次全部成功且无历史单 → 无写操作', async () => {
    const { github, update, create, createComment } = makeGithub({
      issues: [],
      jobs: [{ name: 'Build', conclusion: 'success', html_url: 'u' }],
    });

    await ciNotify({ github, context: ctx(), core: makeCore() });

    expect(update).not.toHaveBeenCalled();
    expect(create).not.toHaveBeenCalled();
    expect(createComment).not.toHaveBeenCalled();
  });

  it('skipped / cancelled 不计入失败 job', async () => {
    const { github, create } = makeGithub({
      issues: [],
      jobs: [
        { name: 'A', conclusion: 'skipped', html_url: 'u1' },
        { name: 'B', conclusion: 'cancelled', html_url: 'u2' },
        { name: 'C', conclusion: 'failure', html_url: 'u3' },
      ],
    });

    await ciNotify({ github, context: ctx(), core: makeCore() });

    expect(create.mock.calls[0][0].body).toContain('[C](u3)');
    expect(create.mock.calls[0][0].body).not.toContain('[A](u1)');
    expect(create.mock.calls[0][0].body).not.toContain('[B](u2)');
  });
});
