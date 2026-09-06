# =============================================================================
# Preflight validation / 安装前检查
# =============================================================================

preflight() {
    local command filesystem index efi_type efi_options
    # Installation requires a root UEFI Live session. / 安装必须在 root 权限的 UEFI Live 环境中运行。
    [[ "$EUID" -eq 0 ]] || die 'Run this installer as root.'
    [[ -d /sys/firmware/efi ]] || die 'The live environment was not booted in UEFI mode.'
    # Keep the dependency list explicit so failures occur before package or storage preparation. / 显式列出依赖，使缺失命令在软件源或存储准备前失败。
    for command in bash cat tee sleep lsblk blockdev sed grep find findmnt wipefs sfdisk blkid mount umount swapon swapoff pacman pacstrap genfstab arch-chroot mktemp mkdir rmdir install cp rm chmod mv sync; do
        require_command "$command"
    done
    # Establish a real, non-symlink target directory before validating storage. / 校验存储前建立真实且非符号链接的目标目录。
    [[ ! -L "$TARGET_ROOT" ]] || die "Target mountpoint must not be a symlink: $TARGET_ROOT"
    mkdir -p -- "$TARGET_ROOT"
    [[ -d "$TARGET_ROOT" ]] || die "Target mountpoint is not a directory: $TARGET_ROOT"
    [[ -f "/usr/share/zoneinfo/$TARGET_TIMEZONE" ]] || die "Timezone data is unavailable: $TARGET_TIMEZONE"
    # Parallel partition arrays must describe the same number of records. / 各分区并行数组必须描述相同数量的记录。
    [[ "${#PART_DEVICES[@]}" -eq "${#PART_USAGES[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_DISK_INDEXES[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_ACTIONS[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_FILESYSTEMS[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_MOUNT_PROFILES[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_MOUNTPOINTS[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_FS_UUIDS[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_UUIDS[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_TYPES[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_NUMBERS[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_START_SECTORS[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_SIZES[@]}" ]] || die 'Partition plan arrays are inconsistent.'
    # Disk identity and automatic-layout arrays follow the same invariant. / 磁盘身份与自动布局数组遵循相同的不变量。
    [[ "${#INSTALL_DISKS[@]}" -gt 0 &&
       "${#INSTALL_DISKS[@]}" -eq "${#DISK_MODELS[@]}" &&
       "${#INSTALL_DISKS[@]}" -eq "${#DISK_SERIALS[@]}" &&
       "${#INSTALL_DISKS[@]}" -eq "${#DISK_PTTYPES[@]}" &&
       "${#INSTALL_DISKS[@]}" -eq "${#DISK_MODES[@]}" &&
       "${#INSTALL_DISKS[@]}" -eq "${#DISK_SIZES[@]}" &&
       "${#INSTALL_DISKS[@]}" -eq "${#DISK_EFI_SIZE_MIB[@]}" &&
       "${#INSTALL_DISKS[@]}" -eq "${#DISK_ROOT_SIZE_MIB[@]}" &&
       "${#INSTALL_DISKS[@]}" -eq "${#DISK_HOME_SIZE_MIB[@]}" &&
       "${#INSTALL_DISKS[@]}" -eq "${#DISK_SWAP_SIZE_MIB[@]}" ]] || die 'Disk plan arrays are inconsistent.'
    # Validate live device identities before selecting filesystem tools. / 选择文件系统工具前校验实时设备身份。
    verify_storage_state
    # Formatter requirements depend only on records that will actually recreate a filesystem. / 格式化工具只由真正重建文件系统的记录决定。
    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do
        [[ "${PART_ACTIONS[index]}" != keep ]] || continue
        filesystem=${PART_FILESYSTEMS[index]}
        case "$filesystem" in
            vfat) require_command mkfs.fat ;;
            ext4) require_command mkfs.ext4 ;;
            xfs) require_command mkfs.xfs ;;
            f2fs) require_command mkfs.f2fs ;;
            swap) require_command mkswap ;;
            *) die "Unsupported filesystem: $filesystem" ;;
        esac
    done
    # Firmware entry creation additionally needs writable efivarfs. / 创建固件启动项还要求 efivarfs 可写。
    if [[ "$CREATE_EFI_ENTRY" == true ]]; then
        [[ -d /sys/firmware/efi/efivars ]] || die 'EFI variables are unavailable.'
        efi_type=$(findmnt -rn -T /sys/firmware/efi/efivars -o FSTYPE)
        efi_options=$(findmnt -rn -T /sys/firmware/efi/efivars -o OPTIONS)
        [[ "$efi_type" == efivarfs && ",$efi_options," != *,ro,* ]] ||
            die 'EFI variables are not mounted read-write.'
    fi
    # Optional inputs are resolved only after the common environment passes. / 通用环境检查通过后再解析可选输入。
    # Mirror discovery stays read-only here; repository mounting happens only after consent. / 此处的镜像探测保持只读；仓库只会在用户确认后挂载。
    [[ "$USE_LOCAL_MIRROR" != true ]] || select_local_mirror_source
    verify_secure_boot_assets
}
