preflight() {
    local command filesystem index efi_type efi_options
    [[ "$EUID" -eq 0 ]] || die 'Run this installer as root.'
    [[ -d /sys/firmware/efi ]] || die 'The live environment was not booted in UEFI mode.'
    for command in bash tee sleep lsblk blockdev sed grep find findmnt wipefs sfdisk blkid mount umount swapon swapoff pacman pacstrap genfstab arch-chroot mktemp mkdir rmdir install cp rm chmod mv sync; do
        require_command "$command"
    done
    [[ ! -L "$TARGET_ROOT" ]] || die "Target mountpoint must not be a symlink: $TARGET_ROOT"
    mkdir -p -- "$TARGET_ROOT"
    [[ -d "$TARGET_ROOT" ]] || die "Target mountpoint is not a directory: $TARGET_ROOT"
    [[ -f "/usr/share/zoneinfo/$TARGET_TIMEZONE" ]] || die "Timezone data is unavailable: $TARGET_TIMEZONE"
    [[ "${#PART_DEVICES[@]}" -eq "${#PART_USAGES[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_DISK_INDEXES[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_ACTIONS[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_FILESYSTEMS[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_F2FS_MODES[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_MOUNTPOINTS[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_FS_UUIDS[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_UUIDS[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_TYPES[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_NUMBERS[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_START_SECTORS[@]}" &&
       "${#PART_DEVICES[@]}" -eq "${#PART_SIZES[@]}" ]] || die 'Partition plan arrays are inconsistent.'
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
    verify_storage_state
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
    if [[ "$CREATE_EFI_ENTRY" == true ]]; then
        [[ -d /sys/firmware/efi/efivars ]] || die 'EFI variables are unavailable.'
        efi_type=$(findmnt -rn -T /sys/firmware/efi/efivars -o FSTYPE)
        efi_options=$(findmnt -rn -T /sys/firmware/efi/efivars -o OPTIONS)
        [[ "$efi_type" == efivarfs && ",$efi_options," != *,ro,* ]] ||
            die 'EFI variables are not mounted read-write.'
    fi
    [[ "$USE_LOCAL_MIRROR" != true ]] || select_local_mirror_source
    verify_secure_boot_assets
}

