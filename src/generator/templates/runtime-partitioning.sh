partition_disk() {
    local index disk mode efi_size root_size home_size swap_size
    for ((index=0; index<${#INSTALL_DISKS[@]}; ++index)); do
        disk=${INSTALL_DISKS[index]}
        mode=${DISK_MODES[index]}
        [[ "$mode" != existing ]] || continue
        efi_size=${DISK_EFI_SIZE_MIB[index]}
        root_size=${DISK_ROOT_SIZE_MIB[index]}
        home_size=${DISK_HOME_SIZE_MIB[index]}
        swap_size=${DISK_SWAP_SIZE_MIB[index]}
    case "$mode" in
        auto-root-swap)
            (( efi_size > 0 && root_size > 0 && swap_size > 0 )) ||
                die 'Automatic partition sizes are invalid.'
            ;;
        auto-home-swap)
            (( efi_size > 0 && root_size > 0 && home_size > 0 && swap_size > 0 )) ||
                die 'Automatic partition sizes are invalid.'
            ;;
        auto-root-only)
            (( efi_size > 0 && DISK_SIZES[index] > (efi_size + 8192) * 1048576 )) ||
                die 'Automatic root-only sizes are invalid.'
            ;;
        auto-data) ;;
        *) die "Unknown storage mode: $mode" ;;
    esac
    phase "Rebuilding the GPT partition table on $disk"
    wipefs --all --force "$disk"
    case "$mode" in
        auto-root-swap)
            sfdisk --wipe always --wipe-partitions always "$disk" <<SFDISK
label: gpt
size=${efi_size}MiB,type=uefi,name="EFI System"
size=${root_size}MiB,type=linux,name="Arch Linux root"
size=${swap_size}MiB,type=swap,name="Linux swap"
SFDISK
            ;;
        auto-home-swap)
            sfdisk --wipe always --wipe-partitions always "$disk" <<SFDISK
label: gpt
size=${efi_size}MiB,type=uefi,name="EFI System"
size=${root_size}MiB,type=linux,name="Arch Linux root"
size=${home_size}MiB,type=linux,name="Arch Linux home"
size=${swap_size}MiB,type=swap,name="Linux swap"
SFDISK
            ;;
        auto-root-only)
            sfdisk --wipe always --wipe-partitions always "$disk" <<SFDISK
label: gpt
size=${efi_size}MiB,type=uefi,name="EFI System"
size=,type=linux,name="Arch Linux root"
SFDISK
            ;;
        auto-data)
            sfdisk --wipe always --wipe-partitions always "$disk" <<SFDISK
label: gpt
size=,type=linux,name="Linux data"
SFDISK
            ;;
        *) die "Unknown storage mode: $mode" ;;
    esac
    if command -v partprobe >/dev/null 2>&1; then
        partprobe "$disk"
    else
        blockdev --rereadpt "$disk"
    fi
    if command -v udevadm >/dev/null 2>&1; then udevadm settle; fi
    done
}

wait_for_partitions() {
    local index device attempt
    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do
        device=${PART_DEVICES[index]}
        for ((attempt=0; attempt<50; ++attempt)); do
            [[ -b "$device" ]] && break
            sleep 0.1
        done
        [[ -b "$device" ]] || die "Partition did not appear: $device"
        if [[ "${DISK_MODES[PART_DISK_INDEXES[index]]}" != existing ]]; then
            verify_created_partition "$device" "${PART_NUMBERS[index]}" \
                "${PART_USAGES[index]}" "${PART_SIZES[index]}" \
                "${PART_DISK_INDEXES[index]}"
        fi
    done
}

