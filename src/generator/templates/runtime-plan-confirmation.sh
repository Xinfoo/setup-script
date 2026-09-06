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
        # Show the captured identity before the separate risk and phrase confirmation. / 在独立的风险与短语确认前显示已捕获的镜像身份。
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

# Confirm the user-supplied shim package trust boundary before any preparation. / 在任何准备操作前确认用户提供的 shim 包信任边界。
confirm_secure_boot_package_source() {
    local answer
    [[ "$ENABLE_SECURE_BOOT" == true ]] || return 0
    [[ -t 0 ]] || die 'Secure Boot package confirmation requires an interactive terminal.'
    printf '\nWARNING: shim-signed.pkg.tar.zst is a user-trusted input.\n'
    printf 'The installer checks its package name and EFI signature presence, but does not authenticate its source or inspect its install scripts.\n'
    printf 'You must independently confirm that this package comes from a trusted source and is usable on this system.\n'
    # The fixed phrase prevents an accidental Enter from accepting this external package. / 固定短语可避免误按 Enter 接受此外部软件包。
    printf 'Type TRUST SHIM-SIGNED to accept responsibility and continue: '
    IFS= read -r answer || die 'Secure Boot package confirmation was interrupted.'
    [[ "$answer" == 'TRUST SHIM-SIGNED' ]] ||
        die 'The shim-signed source and usability were not confirmed.'
}

# Confirm only the local-mirror bootstrap trust boundary. / 仅确认本地镜像引导过程的信任边界。
confirm_package_preparation() {
    local answer
    # Official network repositories proceed directly; only the local bootstrap changes trust policy. / 官方网络仓库直接继续；只有本地引导会改变信任策略。
    [[ "$USE_LOCAL_MIRROR" == true ]] || return 0
    # This confirmation occurs before pacman state in the Live environment changes. / 此确认发生在 Live 环境 pacman 状态变化之前。
    [[ -t 0 ]] || die 'Package preparation requires an interactive terminal.'
    printf '\nWARNING: the local mirror is a user-trusted package source.\n'
    printf 'The installer does not authenticate its contents or guarantee that it is complete, current, compatible, or usable.\n'
    printf 'Packages and their installation hooks can run with root privileges.\n'
    printf 'Bootstrapping the local HTTP server temporarily disables signature verification in the Live environment.\n'
    printf 'The target system keeps its standard package signature policy.\n'
    printf 'Detected source: %s  UUID=%s  parent=%s\n' \
        "$LOCAL_MIRROR_SOURCE" "$LOCAL_MIRROR_UUID" "$LOCAL_MIRROR_PARENT"
    printf 'The source must remain an unused F2FS partition labelled F2FS-DATA, contain repo/archlinux, and stay outside every installation disk.\n'
    # Separate the ordinary decision from the deliberate final acceptance phrase. / 将普通选择与刻意输入的最终接受短语分开。
    while true; do
        printf 'Accept these risks and use this local mirror? [yes/no]: '
        IFS= read -r answer || die 'Preparation confirmation was interrupted.'
        case "${answer,,}" in
            yes) break ;;
            no) die 'Local mirror use was declined.' ;;
            *) printf 'Please enter yes or no.\n' ;;
        esac
    done
    printf 'Type ACCEPT USE LOCAL MIRROR to continue: '
    IFS= read -r answer || die 'Preparation confirmation was interrupted.'
    [[ "$answer" == 'ACCEPT USE LOCAL MIRROR' ]] ||
        die 'Local mirror use was not confirmed.'
    # Bind the accepted identity to the device that will be mounted next. / 将已接受的身份绑定到随后要挂载的设备。
    verify_local_mirror_identity
}

# List every affected block device and require two-stage consent before writes. / 列出全部受影响块设备，并在写入前要求两阶段授权。
confirm_destructive_actions() {
    local answer index disk_index disk model mode action usage operation target
    [[ -t 0 ]] || die 'Destructive confirmation requires an interactive terminal.'
    printf '\nThe following table lists every disk and block device that will be erased, formatted, mounted for writing, or enabled as swap.\n\n'
    printf '%-22s  %-28s  %-22s  %-30s  %s\n' \
        'PARENT DISK' 'DISK MODEL' 'BLOCK DEVICE' 'OPERATIONS' 'TARGET'
    printf '%-22s  %-28s  %-22s  %-30s  %s\n' \
        '-----------' '----------' '------------' '----------' '------'
    # Group whole-disk and partition operations by parent disk. / 按父磁盘对整盘和分区操作分组。
    for ((disk_index=0; disk_index<${#INSTALL_DISKS[@]}; ++disk_index)); do
        disk=${INSTALL_DISKS[disk_index]}
        model=${DISK_MODELS[disk_index]:-unknown}
        mode=${DISK_MODES[disk_index]}
        if [[ "$mode" != existing ]]; then
            # Automatic layouts affect the whole parent disk before partition-level work begins. / 自动布局会在分区级操作开始前影响整块父磁盘。
            printf '%-22s  %-28s  %-22s  %-30s  %s\n' \
                "$disk" "$model" "$disk" 'ERASE+REPARTITION' '-'
        fi
        for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do
            [[ "${PART_DISK_INDEXES[index]}" == "$disk_index" ]] || continue
            action=${PART_ACTIONS[index]}
            usage=${PART_USAGES[index]}
            if [[ "$action" == format ]]; then
                # Planned partitions are created by sfdisk; existing ones only recreate their filesystem. / 计划分区由 sfdisk 新建；现有分区只重建文件系统。
                [[ "$mode" == existing ]] && operation='FORMAT' || operation='CREATE+FORMAT'
            else
                operation='KEEP'
            fi
            case "$usage" in
                # Usage adds the runtime effect that follows the base KEEP/FORMAT operation. / 用途会在基础 KEEP/FORMAT 动作后追加运行时效果。
                swap)
                    operation+='+SWAPON'
                    target='swap'
                    ;;
                unused)
                    target='-'
                    ;;
                *)
                    operation+='+MOUNT/WRITE'
                    target=${PART_MOUNTPOINTS[index]}
                    ;;
            esac
            printf '%-22s  %-28s  %-22s  %-30s  %s\n' \
                "$disk" "$model" "${PART_DEVICES[index]}" "$operation" "$target"
        done
    done
    printf '\nThis plan can destroy data and cannot be rolled back.\n'
    # A plain yes/no decision precedes the deliberately exact final phrase. / 普通 yes/no 选择之后仍需输入精确的最终确认短语。
    while true; do
        printf 'Continue with these storage operations? [yes/no]: '
        IFS= read -r answer || die 'Confirmation input was interrupted.'
        case "${answer,,}" in
            yes) break ;;
            no) die 'Storage execution was declined.' ;;
            *) printf 'Please enter yes or no.\n' ;;
        esac
    done
    printf 'Type CONFIRM EXECUTE to begin storage execution: '
    IFS= read -r answer || die 'Final confirmation input was interrupted.'
    [[ "$answer" == 'CONFIRM EXECUTE' ]] || die 'Final storage confirmation did not match.'
}
