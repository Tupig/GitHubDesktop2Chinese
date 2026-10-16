## 变更说明 / Description

<!-- 做了什么、为什么；关联 Issue 用 Closes #NN / What & why; link issues with Closes #NN -->

## 变更类型 / Type of change

- [ ] 汉化映射（json/localization.json）/ Localization mapping
- [ ] 加载器源码（src/）/ Loader source
- [ ] 文档 / Documentation
- [ ] CI 与工具（.github / tools）/ CI & tooling
- [ ] 其他 / Other

## 自检清单 / Checklist

- [ ] 已读 [Wiki 贡献指南](https://github.com/Tupig/TuPigGHD2Chinese/wiki/贡献指南)；映射变更符合「短含长」排序、问句全角 `？` 等规则 / I have read the [Wiki contribution guide](https://github.com/Tupig/TuPigGHD2Chinese/wiki/贡献指南); mapping changes follow the "short-in-long" ordering, full-width `？` for questions, and related rules
- [ ] 本地验证通过（映射：`TuPigGHD2Chinese.exe --nopause dev --invalidcheck --translationfrombak` 或 `cd tools/auto-maintain && npm run all`；源码：本地编译 + 冒烟运行）/ Verified locally (mappings: `--invalidcheck` or `npm run all`; source: local build + smoke run)
- [ ] 映射 / 源码 / third_party / CMake 变更已在 `docs/ReleaseBody.md` 补充变更行并标注全角版本标记 / Mapping / source / third_party / CMake changes have a new line in `docs/ReleaseBody.md` tagged with the full-width version marker
- [ ] 新增文件为 UTF-8 无 BOM / New files are UTF-8 without BOM
