# =============================================================================
# Plan display and operator confirmation / 计划展示与操作确认
# =============================================================================

print_plan() {
    local index
    printf 'Boot/root disk: %s\n' "$TARGET_DISK"
    printf 'Participating disks:\n'
    for ((index=0; index<${#INSTALL_DISKS[@]}; ++index)); do
        printf '  %-22s %-18s %s bytes  %s\n' "${INSTALL_DISKS[index]}" \
            "${DISK_MODELS[index]:-unknown}" "${DISK_SIZES[index]}" "${DISK_MODES[index]}"
    done
    if [[ "$USE_LOCAL_MIRROR" == true ]]; then
        printf 'Local mirror: %s  UUID=%s  parent=%s  serial=%s\n' \
            "$LOCAL_MIRROR_SOURCE" "$LOCAL_MIRROR_UUID" \
            "$LOCAL_MIRROR_PARENT" "${LOCAL_MIRROR_PARENT_SERIAL:-unknown}"
    fi
    printf '%-22s %-8s %-8s %-7s %s\n' DEVICE ACTION FS USAGE MOUNTPOINT
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
    [[ -t 0 ]] || die 'Package preparation requires an interactive terminal.'
    printf '\nPackage preparation refreshes the Live package databases.\n'
    if [[ "$USE_LOCAL_MIRROR" == true ]]; then
        printf 'WARNING: the selected local repository disables package signature verification.\n'
        printf 'It will mount %s read-only and temporarily edit Live pacman configuration.\n' \
            "$LOCAL_MIRROR_SOURCE"
        printf 'Type UNSIGNED %s %s to trust this exact source: ' \
            "$LOCAL_MIRROR_SOURCE" "$LOCAL_MIRROR_UUID"
        IFS= read -r answer || die 'Preparation confirmation was interrupted.'
        [[ "$answer" == "UNSIGNED $LOCAL_MIRROR_SOURCE $LOCAL_MIRROR_UUID" ]] ||
            die 'Unsigned local-mirror preparation was not confirmed.'
        verify_local_mirror_identity
        return 0
    fi
    printf 'Type PREPARE to continue: '
    IFS= read -r answer || die 'Preparation confirmation was interrupted.'
    [[ "$answer" == PREPARE ]] || die 'Package preparation was not confirmed.'
}

# Require the full target disk path before destructive writes. / 破坏性写入前要求完整输入目标磁盘路径。
confirm_destructive_actions() {
    local answer
    [[ -t 0 ]] || die 'Destructive confirmation requires an interactive terminal.'
    printf '\nThis plan can overwrite filesystems and cannot be rolled back.\n'
    printf 'Type the full target disk path (%s) to continue: ' "$TARGET_DISK"
    IFS= read -r answer || die 'Confirmation input was interrupted.'
    [[ "$answer" == "$TARGET_DISK" ]] || die 'Target disk confirmation did not match.'
}
