// 备份策略判定单元测试(#276)
// 被测: src/utils/backup_policy.hpp（原 EnsureJsFilesAndBackups 内的隐式「只看 .bak 是否存在」逻辑）
// 背景缺陷: 用户删除 .bak 且目标已汉化时, 原逻辑会把汉化内容当原始文件重新备份, 使 --rollback 失效。
#include "utils/backup_policy.hpp"

#include <gtest/gtest.h>

using utils::BackupDecision;
using utils::DecideBackup;

TEST(BackupPolicy, ExistingBackupIsSkipped) {
    // .bak 存在 → 无论标记如何都不新建
    EXPECT_EQ(DecideBackup(true, false), BackupDecision::kSkipBecauseBackupExists);
    EXPECT_EQ(DecideBackup(true, true), BackupDecision::kSkipBecauseBackupExists);
}

TEST(BackupPolicy, FirstRunCreatesBackup) {
    // 首次运行: 无 .bak 且无来源标记 → 目标应为原始英文, 正常备份
    EXPECT_EQ(DecideBackup(false, false), BackupDecision::kCreateBackup);
}

TEST(BackupPolicy, LostBackupWithOriginMarkerIsRefused) {
    // 曾备份过(有标记)但 .bak 已丢失 → 拒绝以当前(可能已汉化)内容冒充原始备份
    EXPECT_EQ(DecideBackup(false, true), BackupDecision::kRefuseOriginalLost);
}
