// Vitest 配置（#263）：测试收集 + 覆盖率度量与门禁
// 覆盖率只统计 src/ 下的产物代码（排除 CLI 编排入口 index.js —— 它由 `npm run all`
// 集成运行覆盖，不属单元测试范围），阈值用于阻止覆盖率劣化。
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
