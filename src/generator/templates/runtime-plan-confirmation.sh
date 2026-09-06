# =============================================================================
# Plan display and operator confirmation / 计划展示与操作确认
# =============================================================================

print_plan() {
    local index
    # Show disk-level identity before the partition-level actions. / 先展示磁盘级身份，再展示分区级操作。
    printf 'Boot/root disk: %s\n' "$TARGET_DISK"
    printf 'Participating disks:\n'
    for ((index=0; index<${#INSTALL_DISKS[@]}; ++index)); do
        printf '  %-22s %-18s %s bytes  %s\n' "${INSTALL_DISKS[index]}" \
            "${DISK_MODELS[index]:-unknown}" "${DISK_SIZES[index]}" "${DISK_MODES[index]}"
    done
    if [[ "$USE_LOCAL_MIRROR" == true ]]; then
        # Include the captured mirror identity used by the confirmation phrase. / 显示确认短语所使用的镜像身份。
        printf 'Local mirror: %s  UUID=%s  parent=%s  serial=%s\n' \
            "$LOCAL_MIRROR_SOURCE" "$LOCAL_MIRROR_UUID" \
            "$LOCAL_MIRROR_PARENT" "${LOCAL_MIRROR_PARENT_SERIAL:-unknown}"
    fi
    printf '%-22s %-8s %-8s %-7s %s\n' DEVICE ACTION FS USAGE MOUNTPOINT
    # Partition rows are already emitted in the plan's deterministic order. / 分区行已按计划中的确定顺序生成。
    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do
        printf '%-22s %-8s %-8s %-7s %s\n' \
            "${PART_DEVICES[index]}" "${PART_ACTIONS[index]}" \
            "${PART_FILESYSTEMS[index]}" "${PART_USAGES[index]}" \
            "${PART_MOUNTPOINTS[index]}"
    done
}

# Confirm repository preparation before package databases are changed. / 更改软件包数据库前确认仓库准备操作。
confirm_package_preparation() {
    local answer
    # This confirmation occurs before pacman state in the Live environment changes. / 此确认发生在 Live 环境 pacman 状态变化之前。
    [[ -t 0 ]] || die 'Package preparation requires an interactive terminal.'
    printf '\nPackage preparation refreshes the Live package databases.\n'
    if [[ "$USE_LOCAL_MIRROR" == true ]]; then
        # Bind consent to both the selected device path and filesystem UUID. / 将授权同时绑定到所选设备路径和文件系统 UUID。
        printf 'WARNING: installing the local HTTP server temporarily disables signature verification in the Live environment.\n'
        printf 'The target system keeps its standard package signature policy.\n'
        printf 'The installer will mount %s read-only and temporarily edit the Live pacman configuration.\n' \
            "$LOCAL_MIRROR_SOURCE"
        printf 'Type BOOTSTRAP %s %s to trust this exact source for the server bootstrap: ' \
            "$LOCAL_MIRROR_SOURCE" "$LOCAL_MIRROR_UUID"
        IFS= read -r answer || die 'Preparation confirmation was interrupted.'
        [[ "$answer" == "BOOTSTRAP $LOCAL_MIRROR_SOURCE $LOCAL_MIRROR_UUID" ]] ||
            die 'Local-mirror server bootstrap was not confirmed.'
        verify_local_mirror_identity
        return 0
    fi
    # Network repositories need a shorter acknowledgement because no local source is trusted. / 网络仓库无需信任本地源，因此使用较短确认词。
    printf 'Type PREPARE to continue: '
    IFS= read -r answer || die 'Preparation confirmation was interrupted.'
    [[ "$answer" == PREPARE ]] || die 'Package preparation was not confirmed.'
}

# Require the full target disk path before destructive writes. / 破坏性写入前要求完整输入目标磁盘路径。
confirm_destructive_actions() {
    local answer
    # Require the complete system-disk path immediately before write-side validation. / 在写入前复核之前要求输入完整系统盘路径。
    [[ -t 0 ]] || die 'Destructive confirmation requires an interactive terminal.'
    printf '\nThis plan can overwrite filesystems and cannot be rolled back.\n'
    printf 'Type the full target disk path (%s) to continue: ' "$TARGET_DISK"
    IFS= read -r answer || die 'Confirmation input was interrupted.'
    [[ "$answer" == "$TARGET_DISK" ]] || die 'Target disk confirmation did not match.'
}
