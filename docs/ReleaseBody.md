
### 1.2.30 起的修复与更新


#### 版本更新日志:
- 版本策略重置: 自 v1.2.30 起仅维护最新版本, 历史变更条目不再保留; 映射 `minversion` 提升至 1.2.30, 旧版加载器加载新映射时将提示需升级（v1.2.30）
- 审计修复: 工具解压完整性检查改为尾部 64KB 搜索 EOCD(兼容含注释归档)、检测工具新增 ReDoS 嵌套量词告警(与 CI 对齐)、网络响应体上限 32MB、解压应用目录查找迭代化（v1.2.30）

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
