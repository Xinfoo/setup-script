# =============================================================================
# Logging and basic runtime helpers / 日志与基础运行时辅助函数
# =============================================================================

start_logged_session() {
    local previous_umask log_fd script_path
    # The inner process already owns a PTY; keep all package-manager streams attached to it. / 内层进程已经拥有 PTY；让软件包管理器的所有流继续连接该终端。
    if [[ "${ARCH_INSTALL_PTY_ACTIVE:-}" == 1 ]]; then
        [[ -t 0 && -t 1 && -t 2 ]] ||
            die 'The logged installer session is not attached to a terminal.'
        [[ -n "$LOG_FILE" && -f "$LOG_FILE" && ! -L "$LOG_FILE" ]] ||
            die 'The installer session log is missing or unsafe.'
        readonly LOG_FILE
        return 0
    fi
    # The outer process creates the log, then script(1) records an output-only PTY session. / 外层进程创建日志，再由 script(1) 记录仅输出的 PTY 会话。
    [[ -t 0 && -t 1 && -t 2 ]] ||
        die 'Run this installer directly from an interactive terminal.'
    [[ -x /usr/bin/script && -x /usr/bin/mktemp ]] ||
        die 'script and mktemp are required for PTY logging.'
    previous_umask=$(umask)
    umask 077
    # User-provided paths must be new files; automatic logs use a private name. / 用户指定路径必须是新文件；自动日志使用私有名称。
    if [[ -n "$LOG_FILE" ]]; then
        [[ ! -e "$LOG_FILE" && ! -L "$LOG_FILE" ]] ||
            die "Refusing to replace existing log path: $LOG_FILE"
        set -o noclobber
        if ! exec {log_fd}> "$LOG_FILE"; then
            set +o noclobber
            umask "$previous_umask"
            die "Cannot create log file safely: $LOG_FILE"
        fi
        set +o noclobber
    else
        LOG_FILE=$(/usr/bin/mktemp /tmp/arch-install.XXXXXX.log) || die 'Cannot create the install log.'
        exec {log_fd}>> "$LOG_FILE" || die 'Cannot open the install log.'
    fi
    umask "$previous_umask"
    script_path=${BASH_SOURCE[0]}
    export ARCH_INSTALL_LOG="$LOG_FILE"
    export ARCH_INSTALL_PTY_ACTIVE=1
    # Reopen only the validated descriptor; --flush persists output continuously and --return preserves the installer status. / 只重新打开已校验描述符；--flush 持续落盘，--return 保留安装器退出状态。
    exec /usr/bin/script --quiet --return --flush --force \
        --log-out "/proc/self/fd/$log_fd" -- /usr/bin/bash "$script_path"
    die 'Cannot start the logged PTY installer session.'
}

# The outer invocation is replaced here; only the inner PTY process reaches cleanup registration. / 外层调用会在这里被替换；只有 PTY 内层进程会继续注册清理。
start_logged_session
# Register cleanup before the inner installer allocates resources or touches disks. / 在内层安装器分配资源或操作磁盘前注册清理逻辑。
trap cleanup EXIT
# Signal exits share the same EXIT cleanup path and retain the conventional status. / 信号退出复用同一 EXIT 清理路径并保留惯例状态码。
trap 'exit 130' INT TERM HUP

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
    # A mounted source must be released explicitly by the operator. / 已挂载的源设备必须由操作者显式释放。
    if findmnt -rn -S "$node" >/dev/null 2>&1; then
        die "$node is mounted; unmount it before running this script"
    else
        mounted_status=$?
        [[ "$mounted_status" -eq 1 ]] || die "Cannot inspect mounts for $node"
    fi
    active_swaps=$(swapon --show=NAME --noheadings --raw 2>/dev/null) ||
        die 'Cannot inspect active swap devices.'
    # Exact whole-line matching avoids confusing similarly prefixed device paths. / 整行精确匹配可避免混淆前缀相似的设备路径。
    if grep -Fxq -- "$node" <<<"$active_swaps"; then
        die "$node is active swap; disable it before running this script"
    fi
    # Kernel holders cover device-mapper, RAID, and similar stacked devices. / 内核 holders 覆盖 device-mapper、RAID 等堆叠设备。
    kernel_name=$(lsblk -dnro KNAME -- "$node")
    holders=/sys/class/block/$kernel_name/holders
    [[ -d "$holders" ]] || die "Cannot inspect block-device holders for $node"
    holder_entry=$(find "$holders" -mindepth 1 -maxdepth 1 -print -quit) ||
        die "Cannot inspect block-device holders for $node"
    if [[ -n "$holder_entry" ]]; then
        die "$node is still held by an active mapped, RAID, or logical device"
    fi
}
