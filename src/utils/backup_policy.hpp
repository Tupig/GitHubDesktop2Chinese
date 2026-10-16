#ifndef UTILS_BACKUP_POLICY_HPP
#define UTILS_BACKUP_POLICY_HPP

// 备份策略判定（自 src/TuPigGHD2Chinese.cpp 抽出，便于单元测试，#276）：
//   何时应当新建 *.bak、何时必须拒绝 —— 纯判定逻辑，无文件系统副作用。
//
// 背景（#276 的 V2 缺陷）：原实现只看「.bak 是否存在」决定是否备份。若用户删除了 .bak
// 而目标文件已是**汉化后**内容，原逻辑会把汉化内容当成「原始文件」重新备份，
// 使 --rollback 回退到汉化内容（备份语义静默失效，且用户以为手里有原始备份）。
//
// 判定依据（调用方传入两个事实）：
//   - .bak 已存在                → 无需新建（保持既有行为）
//   - .bak 缺失 且 无 origin 标记 → 首次运行，目标应为原始英文 → 新建备份（并写 origin 标记）
//   - .bak 缺失 且 有 origin 标记 → 曾经备份过（目标此后可能已被本工具改写）→ **拒绝新建**，
//     以免把汉化内容误当原始备份（调用方告警并跳过备份，不影响本次汉化）
//
// origin 标记：与 .bak 同目录的「<name>.bak.origin」，仅在成功建立备份时写入；
// 该文件名不含 ".writing.<8hex>" 形态，故不会被临时文件清理误删。

namespace utils {

enum class BackupDecision {
    kSkipBecauseBackupExists,  // 已有 .bak，无需处理
    kCreateBackup,             // 首次运行：新建 .bak 并写 origin 标记
    kRefuseOriginalLost,       // .bak 曾存在但已丢失：拒绝以当前内容冒充原始备份
};

inline auto DecideBackup(bool backup_exists, bool origin_marker_exists) -> BackupDecision {
    if(backup_exists) {
        return BackupDecision::kSkipBecauseBackupExists;
    }
    if(origin_marker_exists) {
        return BackupDecision::kRefuseOriginalLost;
    }
    return BackupDecision::kCreateBackup;
}

} // namespace utils

#endif // !UTILS_BACKUP_POLICY_HPP
