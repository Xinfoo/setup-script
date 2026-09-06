# =============================================================================
# Runtime state and common helpers / 运行时状态与通用辅助函数
# =============================================================================

# Resource ownership flags let cleanup touch only objects created by this run. / 资源归属标志确保清理只处理本次运行创建的对象。
WORK_DIR=''
TARGET_MOUNTED=false
LOCAL_MIRROR_MOUNTED=false
LOCAL_MIRROR_SERVER_RUNNING=false
LOCAL_MIRROR_SERVER_PID=''
HOST_PACMAN_CHANGED=false
INSTALL_SUCCEEDED=false
# The local mirror identity is captured before it is trusted or mounted. / 本地镜像身份会在受信任或挂载前被完整记录。
LOCAL_MIRROR_SOURCE=''
LOCAL_MIRROR_UUID=''
LOCAL_MIRROR_PARENT=''
LOCAL_MIRROR_PARENT_SERIAL=''
LOCAL_MIRROR_SIZE=''
# KEEP probing records the exact temporary mount currently owned by the installer. / KEEP 探测记录安装器当前持有的精确临时挂载。
KEEP_PROBE_MOUNT=''
KEEP_PROBE_SOURCE=''
KEEP_PROBE_ACTIVE=false
# Secure Boot secrets live only in the private tmpfs snapshot. / Secure Boot 私密材料只存在于私有 tmpfs 快照中。
SECURE_BOOT_ASSET_SNAPSHOT=''
SECURE_BOOT_SNAPSHOT_MOUNTED=false
SECURE_BOOT_SIGNING_COMPLETE=false
# Logging descriptors remain separate so cleanup messages can reach both destinations. / 日志描述符相互独立，使清理信息能到达控制台和日志。
CONSOLE_FD=''
LOG_FD=''
LOG_TEE_PID=''
# Track reversible runtime actions in the order they are created. / 按创建顺序记录可逆的运行时操作。
SWAPS_TO_DISABLE=()
SECURE_BOOT_STAGED_FILES=()

# Stop immediately with a consistent error message. / 使用统一错误信息立即终止。
die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

# Print a visible installation phase marker. / 输出醒目的安装阶段标记。
phase() {
    printf '\n==> %s\n' "$*"
}
