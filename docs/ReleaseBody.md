### 1.2.30 起的修复与更新

#### 版本更新日志:
- 版本策略重置: 自 v1.2.30 起仅维护最新版本, 历史变更条目不再保留; 映射 `minversion` 提升至 1.2.30, 旧版加载器加载新映射时将提示需升级（v1.2.30）
- 审计修复: 工具解压完整性检查改为尾部 64KB 搜索 EOCD(兼容含注释归档)、检测工具新增 ReDoS 嵌套量词告警(与 CI 对齐)、网络响应体上限 32MB、解压应用目录查找迭代化（v1.2.30）
- 安全与健壮性审计修复: 网络下载与自更新、输入交互、版本解析、系统环境探测等环节共整改 18 项(高危 3 / 中危 5 / 低危 10)（v1.2.31）
- 依赖更新: 随仓预编译 OpenSSL 由 3.3.0(已停止维护) 升级至 3.5.9 LTS, Windows 构建不再联网下载依赖(CMake 移除运行时下载回退, 配置完全离线)（v1.2.32）
- CI 增强: 工具自检新增 Markdown 链接检查; 新增本地冒烟测试脚本 tools/ci/smoke-test.ps1 与三方组件版本清单 third_party/README.md（v1.2.32）
- 工程规范: 文本编码统一为 UTF-8 无 BOM 形态(CMake 配置 / JSON / Markdown / C++ 头文件共 6 处去除 BOM), 冒烟测试脚本补 UTF-8 BOM 兼容本地 Windows PowerShell 5.1 运行（v1.2.33）

<!--
#### 修复BUG:
- 

#### 更新内容:
- 

-->


### 程序说明  
1. 可以仅下载二进制程序,双击运行后自动汉化  
2. 如果运行提示出错,可重试或下载[此JSON文件](https://github.com/Tupig/GitHubDesktop2Chinese/blob/main/json/localization.json)后放在同目录后运行二进制程序  
3. **若使用GitHub 仓库作为json文件源，请升级加载器到最新版本**
4. `GitHubDesktop2Chinese.exe` 为 64 位 Windows 程序，不再提供 32 位版本；Release 页同时提供 macOS 分架构二进制（`macos-x64` / `macos-arm64`）与 Linux x64 版本
5. 如果汉化后主程序无法打开，请更新加载器后执行参数 `GitHubDesktop2Chinese.exe dev --translationfrombak`
