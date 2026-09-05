# =============================================================================
# Logging and basic runtime helpers / 日志与基础运行时辅助函数
# =============================================================================

initialize_log() {
    local previous_umask
    exec {CONSOLE_FD}>&2 || exit 1
    [[ -x /usr/bin/tee && -x /usr/bin/mktemp && -x /usr/bin/sleep ]] ||
        die 'tee, mktemp, and sleep are required for logging.'
    previous_umask=$(umask)
    umask 077
    if [[ -n "$LOG_FILE" ]]; then
        [[ ! -e "$LOG_FILE" && ! -L "$LOG_FILE" ]] ||
            die "Refusing to replace existing log path: $LOG_FILE"
        set -o noclobber
        if ! exec {LOG_FD}> "$LOG_FILE"; then
            set +o noclobber
            umask "$previous_umask"
            die "Cannot create log file safely: $LOG_FILE"
        fi
        set +o noclobber
    else
        LOG_FILE=$(/usr/bin/mktemp /tmp/arch-install.XXXXXX.log) || die 'Cannot create the install log.'
        exec {LOG_FD}>> "$LOG_FILE" || die 'Cannot open the install log.'
    fi
    umask "$previous_umask"
    readonly LOG_FILE
    exec > >(/usr/bin/tee -a "/proc/self/fd/$LOG_FD") 2>&1
    LOG_TEE_PID=$!
}

# Register cleanup before starting logging or touching disks. / 在记录日志或操作磁盘前注册清理逻辑。
trap cleanup EXIT
trap 'exit 130' INT TERM HUP
initialize_log

# Verify external dependencies before use. / 在使用外部命令前确认其存在。
require_command() {
    command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1"
}

# Normalize filesystem aliases reported by system tools. / 统一系统工具返回的文件系统别名。
normalize_fs() {
    case "${1,,}" in
        fat|fat16|fat32|vfat) printf 'vfat' ;;
        *) printf '%s' "${1,,}" ;;
    esac
}

# Reject mounted, active-swap, or held block devices. / 拒绝已挂载、已启用交换或仍被占用的块设备。
ensure_node_idle() {
    local node=$1 kernel_name holders mounted_status active_swaps holder_entry
    if findmnt -rn -S "$node" >/dev/null 2>&1; then
        die "$node is mounted; unmount it before running this script"
    else
        mounted_status=$?
        [[ "$mounted_status" -eq 1 ]] || die "Cannot inspect mounts for $node"
    fi
    active_swaps=$(swapon --show=NAME --noheadings --raw 2>/dev/null) ||
        die 'Cannot inspect active swap devices.'
    if grep -Fxq -- "$node" <<<"$active_swaps"; then
        die "$node is active swap; disable it before running this script"
    fi
    kernel_name=$(lsblk -dnro KNAME -- "$node")
    holders=/sys/class/block/$kernel_name/holders
    [[ -d "$holders" ]] || die "Cannot inspect block-device holders for $node"
    holder_entry=$(find "$holders" -mindepth 1 -maxdepth 1 -print -quit) ||
        die "Cannot inspect block-device holders for $node"
    if [[ -n "$holder_entry" ]]; then
        die "$node is still held by an active mapped, RAID, or logical device"
    fi
}
