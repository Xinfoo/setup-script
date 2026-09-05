# =============================================================================
# Failure recovery and resource cleanup / 失败恢复与资源清理
# =============================================================================

cleanup() {
    local status=$?
    local index logger_watchdog query_status cleanup_failed=false
    local target_active=false mirror_active=false snapshot_active=false active_swaps=''
    local snapshot_remove_safe=true
    local mounted_source=''
    trap - EXIT INT TERM HUP
    set +e
    # Identify only resources still owned by this run. / 只识别仍由本次运行持有的资源。
    if command -v findmnt >/dev/null 2>&1; then
        if [[ "$KEEP_PROBE_ACTIVE" == true && -n "$KEEP_PROBE_MOUNT" ]]; then
            if mounted_source=$(findmnt -rn --mountpoint "$KEEP_PROBE_MOUNT" -o SOURCE); then
                if [[ "$mounted_source" != "$KEEP_PROBE_SOURCE" ]]; then
                    printf 'WARNING: refusing to clean a foreign KEEP probe mount.\n' >&2
                    cleanup_failed=true
                elif ! umount -- "$KEEP_PROBE_MOUNT"; then
                    printf 'WARNING: failed to unmount the KEEP probe.\n' >&2
                    cleanup_failed=true
                fi
            else
                query_status=$?
                if [[ "$query_status" -ne 1 ]]; then
                    printf 'WARNING: could not inspect the KEEP probe during cleanup.\n' >&2
                    cleanup_failed=true
                fi
            fi
        fi
        if [[ "$TARGET_MOUNTED" == true ]]; then
            if mounted_source=$(findmnt -rn --mountpoint "$TARGET_ROOT" -o SOURCE); then
                if [[ "$mounted_source" == "$ROOT_DEVICE" ]]; then
                    target_active=true
                else
                    printf 'WARNING: refusing to clean a foreign mount at %s.\n' "$TARGET_ROOT" >&2
                    cleanup_failed=true
                fi
            else
                query_status=$?
                if [[ "$query_status" -ne 1 ]]; then
                    printf 'WARNING: could not inspect the target mount during cleanup.\n' >&2
                    cleanup_failed=true
                fi
            fi
        fi
        if [[ "$LOCAL_MIRROR_MOUNTED" == true ]]; then
            if mounted_source=$(findmnt -rn --mountpoint /run/media/root/F2FS-DATA -o SOURCE); then
                if [[ "$mounted_source" == "$LOCAL_MIRROR_SOURCE" ]]; then
                    mirror_active=true
                else
                    printf 'WARNING: refusing to clean a foreign local-mirror mount.\n' >&2
                    cleanup_failed=true
                fi
            else
                query_status=$?
                if [[ "$query_status" -ne 1 ]]; then
                    printf 'WARNING: could not inspect the local-mirror mount during cleanup.\n' >&2
                    cleanup_failed=true
                fi
            fi
        fi
        if [[ "$SECURE_BOOT_SNAPSHOT_MOUNTED" == true &&
              -n "$SECURE_BOOT_ASSET_SNAPSHOT" ]]; then
            if mounted_source=$(findmnt -rn --mountpoint \
                "$SECURE_BOOT_ASSET_SNAPSHOT" -o SOURCE,FSTYPE); then
                if [[ "$mounted_source" == 'tmpfs tmpfs' ]]; then
                    snapshot_active=true
                else
                    printf 'WARNING: refusing to clean a foreign Secure Boot snapshot mount.\n' >&2
                    cleanup_failed=true
                    snapshot_remove_safe=false
                fi
            else
                query_status=$?
                if [[ "$query_status" -ne 1 ]]; then
                    printf 'WARNING: could not inspect the Secure Boot snapshot during cleanup.\n' >&2
                    cleanup_failed=true
                    snapshot_remove_safe=false
                fi
            fi
        fi
    fi
    # Restore temporary target repository changes after a failed run. / 安装失败时恢复目标系统的临时仓库改动。
    if [[ "$status" -ne 0 && "$target_active" == true &&
          "$TARGET_CONFIG_FINALIZED" != true && -n "$WORK_DIR" ]]; then
        if [[ -f "$WORK_DIR/target-pacman.conf" ]]; then
            if ! cp -a -- "$WORK_DIR/target-pacman.conf" "$TARGET_ROOT/etc/pacman.conf"; then
                printf 'WARNING: failed to restore target pacman.conf.\n' >&2
                cleanup_failed=true
            fi
        fi
        if [[ -f "$WORK_DIR/target-mirrorlist" ]]; then
            if ! cp -a -- "$WORK_DIR/target-mirrorlist" "$TARGET_ROOT/etc/pacman.d/mirrorlist"; then
                printf 'WARNING: failed to restore target mirrorlist.\n' >&2
                cleanup_failed=true
            fi
        fi
    fi
    # Remove staged files before recursively unmounting the target. / 递归卸载目标系统前移除暂存文件。
    if [[ "$target_active" == true ]]; then
        for index in "${!SECURE_BOOT_STAGED_FILES[@]}"; do
            if [[ -n "${SECURE_BOOT_STAGED_FILES[index]}" ]] &&
               ! rm -f -- "${SECURE_BOOT_STAGED_FILES[index]}"; then
                printf 'WARNING: failed to remove a staged Secure Boot file.\n' >&2
                cleanup_failed=true
            fi
        done
        if ! rm -f -- "$TARGET_ROOT/root/.arch-install-chroot.sh"; then
            printf 'WARNING: failed to remove temporary target files.\n' >&2
            cleanup_failed=true
        fi
        if ! umount -R -- "$TARGET_ROOT"; then
            printf 'WARNING: failed to unmount %s.\n' "$TARGET_ROOT" >&2
            cleanup_failed=true
        fi
    fi
    # Disable only swap devices enabled by this installer. / 仅关闭本安装器启用的交换设备。
    if (( ${#SWAPS_TO_DISABLE[@]} > 0 )); then
        if ! command -v swapon >/dev/null 2>&1 ||
           ! active_swaps=$(swapon --show=NAME --noheadings --raw 2>/dev/null); then
            printf 'WARNING: failed to inspect swap state during cleanup.\n' >&2
            cleanup_failed=true
        fi
    fi
    for ((index=${#SWAPS_TO_DISABLE[@]} - 1; index >= 0; --index)); do
        if grep -Fxq -- "${SWAPS_TO_DISABLE[index]}" <<<"$active_swaps" &&
           ! swapoff -- "${SWAPS_TO_DISABLE[index]}"; then
            printf 'WARNING: failed to disable swap %s.\n' "${SWAPS_TO_DISABLE[index]}" >&2
            cleanup_failed=true
        fi
    done
    # Release the Live-side local mirror mount. / 释放 Live 环境中的本地镜像挂载。
    if [[ "$mirror_active" == true ]]; then
        if ! umount -- '/run/media/root/F2FS-DATA'; then
            printf 'WARNING: failed to unmount the local mirror.\n' >&2
            cleanup_failed=true
        fi
    fi
    # Restore the Live environment package configuration. / 恢复 Live 环境的软件包配置。
    if [[ "$HOST_PACMAN_CHANGED" == true && -n "$WORK_DIR" ]]; then
        if ! cp -a -- "$WORK_DIR/host-pacman.conf" /etc/pacman.conf; then
            printf 'WARNING: failed to restore Live pacman.conf.\n' >&2
            cleanup_failed=true
        fi
        if ! cp -a -- "$WORK_DIR/host-mirrorlist" /etc/pacman.d/mirrorlist; then
            printf 'WARNING: failed to restore the Live mirrorlist.\n' >&2
            cleanup_failed=true
        fi
    fi
    # Unmount and erase the private Secure Boot snapshot when safe. / 在安全条件满足时卸载并清除 Secure Boot 私有快照。
    if [[ "$snapshot_active" == true ]]; then
        if ! umount -- "$SECURE_BOOT_ASSET_SNAPSHOT"; then
            printf 'WARNING: failed to unmount the private Secure Boot snapshot.\n' >&2
            cleanup_failed=true
        else
            snapshot_active=false
            SECURE_BOOT_SNAPSHOT_MOUNTED=false
        fi
    fi
    if [[ "$snapshot_active" != true && "$snapshot_remove_safe" == true &&
          -n "$SECURE_BOOT_ASSET_SNAPSHOT" &&
          -d "$SECURE_BOOT_ASSET_SNAPSHOT" ]]; then
        if ! rm -rf -- "$SECURE_BOOT_ASSET_SNAPSHOT"; then
            printf 'WARNING: failed to remove the private Secure Boot snapshot.\n' >&2
            cleanup_failed=true
        else
            SECURE_BOOT_ASSET_SNAPSHOT=''
        fi
    fi
    # Preserve recovery data when cleanup itself fails. / 清理过程失败时保留恢复数据。
    if [[ -n "$WORK_DIR" && -d "$WORK_DIR" ]]; then
        if [[ "$cleanup_failed" == true ]]; then
            printf 'WARNING: preserving recovery files in %s.\n' "$WORK_DIR" >&2
        elif ! rm -rf -- "$WORK_DIR"; then
            printf 'WARNING: failed to remove temporary files: %s\n' "$WORK_DIR" >&2
            cleanup_failed=true
        fi
    fi
    # Drain the logger last so all cleanup diagnostics reach the log. / 最后结束日志进程，确保清理诊断全部写入日志。
    if [[ "$status" -eq 0 && "$cleanup_failed" == true ]]; then status=1; fi
    if [[ -n "$LOG_TEE_PID" ]]; then
        if [[ -n "$CONSOLE_FD" ]]; then exec 1>&$CONSOLE_FD 2>&1; fi
        ( /usr/bin/sleep 5; kill -TERM "$LOG_TEE_PID" 2>/dev/null ) &
        logger_watchdog=$!
        if ! wait "$LOG_TEE_PID"; then
            printf 'WARNING: the install logger exited unsuccessfully.\n' >&2
            [[ "$status" -ne 0 ]] || status=1
        fi
        kill -TERM "$logger_watchdog" 2>/dev/null
        wait "$logger_watchdog" 2>/dev/null
    fi
    if [[ "$status" -ne 0 ]]; then
        printf '\nInstallation failed (exit %d). Log: %s\n' "$status" "${LOG_FILE:-unavailable}" >&2
    elif [[ "$INSTALL_SUCCEEDED" == true ]]; then
        printf '\nTarget filesystems were unmounted cleanly. Log: %s\n' "$LOG_FILE"
    fi
    if [[ -n "$LOG_FD" ]]; then exec {LOG_FD}>&-; fi
    if [[ -n "$CONSOLE_FD" ]]; then exec {CONSOLE_FD}>&-; fi
    exit "$status"
}
