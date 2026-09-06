# =============================================================================
# Partition table creation and device discovery / 分区表创建与设备发现
# =============================================================================

partition_disk() {
    local index disk mode efi_size root_size home_size swap_size
    # Existing-layout disks are never passed to wipefs or sfdisk. / 现有布局磁盘绝不会交给 wipefs 或 sfdisk。
    for ((index=0; index<${#INSTALL_DISKS[@]}; ++index)); do
        disk=${INSTALL_DISKS[index]}
        mode=${DISK_MODES[index]}
        [[ "$mode" != existing ]] || continue
        efi_size=${DISK_EFI_SIZE_MIB[index]}
        root_size=${DISK_ROOT_SIZE_MIB[index]}
        home_size=${DISK_HOME_SIZE_MIB[index]}
        swap_size=${DISK_SWAP_SIZE_MIB[index]}
    # Validate every computed layout before erasing the current partition table. / 擦除当前分区表前验证所有计算出的布局参数。
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
    # Rebuild one GPT at a time so progress identifies the affected disk. / 每次重建一块 GPT，使进度信息明确对应磁盘。
    phase "Rebuilding the GPT partition table on $disk"
    wipefs --all --force "$disk"
    # sfdisk receives the exact layout selected for this disk. / sfdisk 接收为当前磁盘选择的精确布局。
    case "$mode" in
        auto-root-swap)
            sfdisk --wipe always --wipe-partitions always "$disk" <<SFDISK
label: gpt
size=${efi_size}MiB,type=uefi
size=${root_size}MiB,type=linux
size=${swap_size}MiB,type=swap
SFDISK
            ;;
        auto-home-swap)
            sfdisk --wipe always --wipe-partitions always "$disk" <<SFDISK
label: gpt
size=${efi_size}MiB,type=uefi
size=${root_size}MiB,type=linux
size=${home_size}MiB,type=linux
size=${swap_size}MiB,type=swap
SFDISK
            ;;
        auto-root-only)
            sfdisk --wipe always --wipe-partitions always "$disk" <<SFDISK
label: gpt
size=${efi_size}MiB,type=uefi
size=,type=linux
SFDISK
            ;;
        auto-data)
            sfdisk --wipe always --wipe-partitions always "$disk" <<SFDISK
label: gpt
size=,type=linux
SFDISK
            ;;
        *) die "Unknown storage mode: $mode" ;;
    esac
    # Ask the kernel to expose the new table before waiting for device nodes. / 等待设备节点前要求内核重新读取新分区表。
    if command -v partprobe >/dev/null 2>&1; then
        partprobe "$disk"
    else
        blockdev --rereadpt "$disk"
    fi
    # Let udev finish any queued partition events when it is available. / udev 可用时等待其完成已排队的分区事件。
    if command -v udevadm >/dev/null 2>&1; then udevadm settle; fi
    done
}

# Wait for udev to expose every partition named by the plan. / 等待 udev 创建计划中的全部分区设备。
wait_for_partitions() {
    local index device attempt
    # Poll briefly because kernel partition nodes can appear asynchronously. / 短暂轮询，因为内核分区节点可能异步出现。
    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do
        device=${PART_DEVICES[index]}
        for ((attempt=0; attempt<50; ++attempt)); do
            [[ -b "$device" ]] && break
            sleep 0.1
        done
        [[ -b "$device" ]] || die "Partition did not appear: $device"
        # Newly created partitions must match the geometry encoded in the plan. / 新建分区必须匹配计划中编码的几何信息。
        if [[ "${DISK_MODES[PART_DISK_INDEXES[index]]}" != existing ]]; then
            verify_created_partition "$device" "${PART_NUMBERS[index]}" \
                "${PART_USAGES[index]}" "${PART_SIZES[index]}" \
                "${PART_DISK_INDEXES[index]}"
        fi
    done
}
