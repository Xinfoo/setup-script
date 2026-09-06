# =============================================================================
# Failure recovery and resource cleanup / 失败恢复与资源清理
# =============================================================================

cleanup() {
    local status=$?
    local index logger_watchdog query_status cleanup_failed=false
    local target_active=false mirror_active=false snapshot_active=false active_swaps=''
    local snapshot_remove_safe=true
    local mounted_source=''
    # Disable recursive traps and error-exit while collecting cleanup failures. / 收集清理失败时禁用递归 trap 和遇错退出。
    trap - EXIT INT TERM HUP
    set +e
    # Cleanup is best-effort: record every failure instead of stopping at the first one. / 清理采用尽力而为策略：记录全部失败而非在首个错误处停止。
    # Identify only resources still owned by this run. / 只识别仍由本次运行持有的资源。
    if command -v findmnt >/dev/null 2>&1; then
        # A KEEP probe is owned only when its mounted source still matches. / 仅当挂载源仍匹配时才认为 KEEP 探测由本次运行持有。
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
        # Root cleanup is allowed only when /mnt still points to the planned root device. / 仅当 /mnt 仍指向计划根设备时才允许清理目标树。
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
        # Treat the fixed mirror mountpoint as foreign if its source has changed. / 固定镜像挂载点来源变化时将其视为外部资源。
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
        # Verify both tmpfs source and type before handling the key snapshot. / 处理密钥快照前同时校验 tmpfs 来源与类型。
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
    # Remove staged files before recursively unmounting the target. / 递归卸载目标系统前移除暂存文件。
    if [[ "$target_active" == true ]]; then
        # Atomic Secure Boot destinations may leave a named stage after interruption. / Secure Boot 原子写入被中断时可能留下已记录的暂存文件。
        for index in "${!SECURE_BOOT_STAGED_FILES[@]}"; do
            if [[ -n "${SECURE_BOOT_STAGED_FILES[index]}" ]] &&
               ! rm -f -- "${SECURE_BOOT_STAGED_FILES[index]}"; then
                printf 'WARNING: failed to remove a staged Secure Boot file.\n' >&2
                cleanup_failed=true
            fi
        done
        # Remove both chroot payloads whether configuration succeeded or failed. / 无论配置成功或失败都删除两个 chroot 临时载荷。
        if ! rm -f -- \
            "$TARGET_ROOT/root/.arch-install-chroot.sh" \
            "$TARGET_ROOT/root/.arch-install-shim-signed.pkg.tar.zst"; then
            printf 'WARNING: failed to remove temporary target files.\n' >&2
            cleanup_failed=true
        fi
        # Recursive unmount releases child mountpoints before the root. / 递归卸载会先释放子挂载点再释放根挂载。
        if ! umount -R -- "$TARGET_ROOT"; then
            printf 'WARNING: failed to unmount %s.\n' "$TARGET_ROOT" >&2
            cleanup_failed=true
        fi
    fi
    # Disable only swap devices enabled by this installer. / 仅关闭本安装器启用的交换设备。
    if (( ${#SWAPS_TO_DISABLE[@]} > 0 )); then
        # Snapshot active swap names once before walking our list in reverse. / 逆序遍历本次列表前一次性获取活动 Swap 名称。
        if ! command -v swapon >/dev/null 2>&1 ||
           ! active_swaps=$(swapon --show=NAME --noheadings --raw 2>/dev/null); then
            printf 'WARNING: failed to inspect swap state during cleanup.\n' >&2
            cleanup_failed=true
        fi
    fi
    # Reverse order mirrors the order in which swap devices were enabled. / 逆序与 Swap 设备的启用顺序相对应。
    for ((index=${#SWAPS_TO_DISABLE[@]} - 1; index >= 0; --index)); do
        if grep -Fxq -- "${SWAPS_TO_DISABLE[index]}" <<<"$active_swaps" &&
           ! swapoff -- "${SWAPS_TO_DISABLE[index]}"; then
            printf 'WARNING: failed to disable swap %s.\n' "${SWAPS_TO_DISABLE[index]}" >&2
            cleanup_failed=true
        fi
    done
    # Stop the loopback HTTP mirror before releasing its source filesystem. / 卸载源文件系统前停止回环 HTTP 镜像。
    if [[ "$LOCAL_MIRROR_SERVER_RUNNING" == true ]] && ! stop_local_mirror_server; then
        printf 'WARNING: failed to stop the temporary local mirror server.\n' >&2
        cleanup_failed=true
    fi
    # Release the Live-side local mirror mount. / 释放 Live 环境中的本地镜像挂载。
    if [[ "$mirror_active" == true ]]; then
        if ! umount -- '/run/media/root/F2FS-DATA'; then
            printf 'WARNING: failed to unmount the local mirror.\n' >&2
            cleanup_failed=true
        fi
    fi
    # Restore the Live environment package configuration. / 恢复 Live 环境的软件包配置。
    if [[ "$HOST_PACMAN_CHANGED" == true && -n "$WORK_DIR" ]]; then
        # Restore both policy and repository selection from private backups. / 从私有备份恢复签名策略和仓库选择。
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
        # Removal is attempted only after the verified tmpfs mount is released. / 仅在已验证的 tmpfs 挂载释放后尝试删除目录。
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
        # Never recurse into a path still known to be mounted or whose ownership is uncertain. / 对仍已知处于挂载状态或归属不明的路径绝不递归删除。
        if ! rm -rf -- "$SECURE_BOOT_ASSET_SNAPSHOT"; then
            printf 'WARNING: failed to remove the private Secure Boot snapshot.\n' >&2
            cleanup_failed=true
        else
            SECURE_BOOT_ASSET_SNAPSHOT=''
        fi
    fi
    # Preserve recovery data when cleanup itself fails. / 清理过程失败时保留恢复数据。
    if [[ -n "$WORK_DIR" && -d "$WORK_DIR" ]]; then
        # Successful cleanup removes the workspace; failed cleanup keeps diagnostics. / 清理成功时删除工作目录；失败时保留诊断材料。
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
        # Restore terminal output, then bound the wait for tee with a watchdog. / 恢复终端输出，并用 watchdog 限制等待 tee 的时间。
        if [[ -n "$CONSOLE_FD" ]]; then exec 1>&$CONSOLE_FD 2>&1; fi
        ( /usr/bin/sleep 5; kill -TERM "$LOG_TEE_PID" 2>/dev/null ) &
        logger_watchdog=$!
        if ! wait "$LOG_TEE_PID"; then
            printf 'WARNING: the install logger exited unsuccessfully.\n' >&2
            [[ "$status" -ne 0 ]] || status=1
        fi
        kill -TERM "$logger_watchdog" 2>/dev/null
        # Reap the watchdog regardless of whether it already fired. / 无论 watchdog 是否已经触发都回收其子进程。
        wait "$logger_watchdog" 2>/dev/null
    fi
    # Report one final outcome after resource and logger cleanup. / 资源与日志清理结束后报告唯一的最终结果。
    if [[ "$status" -ne 0 ]]; then
        printf '\nInstallation failed (exit %d). Log: %s\n' "$status" "${LOG_FILE:-unavailable}" >&2
    elif [[ "$INSTALL_SUCCEEDED" == true ]]; then
        printf '\nTarget filesystems were unmounted cleanly. Log: %s\n' "$LOG_FILE"
    fi
    # Close inherited descriptors explicitly before returning the original status. / 返回原始状态前显式关闭继承的描述符。
    if [[ -n "$LOG_FD" ]]; then exec {LOG_FD}>&-; fi
    if [[ -n "$CONSOLE_FD" ]]; then exec {CONSOLE_FD}>&-; fi
    exit "$status"
}
