// Vitest 配置（#263）：测试收集 + 覆盖率度量与门禁
// 覆盖率仅统计本 root 内（tools/auto-maintain/src/**）的产物代码；排除 CLI 编排入口
// index.js（由 `npm run all` 集成运行覆盖，不属单元测试范围）。阈值用于阻止覆盖率劣化。
// 注：tools/ci 下的 report-issue.js / ci-failure-notify.js 不在本 root 内，无法纳入覆盖率
// 统计（vitest 只接受 root 内路径），由 test/ci-issue-handlers.test.mjs 的 12 个行为用例保障。
import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    include: ['test/**/*.test.mjs'],
    coverage: {
      provider: 'v8',
      reporter: ['text', 'text-summary'],
      include: ['src/**/*.js'],
      exclude: ['src/index.js'],
      thresholds: {
        statements: 85,
        branches: 80,
        functions: 88,
        lines: 87,
      },
    },
  },
});
