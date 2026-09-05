# =============================================================================
# Storage identity and live-state validation / 存储身份与实时状态校验
# =============================================================================

verify_disk_identities() {
    local index disk current_size current_model current_serial current_pttype current_type
    for ((index=0; index<${#INSTALL_DISKS[@]}; ++index)); do
        disk=${INSTALL_DISKS[index]}
        [[ -b "$disk" ]] || die "Installation disk is not a block device: $disk"
        current_type=$(lsblk -dnro TYPE -- "$disk")
        [[ "$current_type" == disk ]] || die "Installation target is not a whole disk: $disk"
        [[ "$(blockdev --getro "$disk")" == 0 ]] || die "Installation disk is read-only: $disk"
        current_size=$(blockdev --getsize64 "$disk")
        [[ "$current_size" == "${DISK_SIZES[index]}" ]] ||
            die "Installation disk size changed: $disk"
        current_model=$(lsblk -dno MODEL -- "$disk" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
        current_serial=$(lsblk -dno SERIAL -- "$disk" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
        current_pttype=$(lsblk -dnro PTTYPE -- "$disk")
        if [[ -n "${DISK_MODELS[index]}" && "$current_model" != "${DISK_MODELS[index]}" ]]; then
            die "Installation disk model changed: $disk"
        fi
        if [[ -n "${DISK_SERIALS[index]}" && "$current_serial" != "${DISK_SERIALS[index]}" ]]; then
            die "Installation disk serial changed: $disk"
        fi
        if [[ "${DISK_MODES[index]}" == existing &&
              "${current_pttype,,}" != "${DISK_PTTYPES[index],,}" ]]; then
            die "Installation disk partition-table type changed: $disk"
        fi
    done
}

# Match an existing partition against its recorded identity. / 将已有分区与计划中记录的身份进行匹配。
verify_existing_partition() {
    local device=$1 expected_parent=$2 expected_number=$3 expected_uuid=$4 expected_start=$5 expected_size=$6 expected_type=$7
    local parent actual_number actual_uuid actual_start actual_size actual_type
    [[ -b "$device" ]] || die "Configured partition is missing: $device"
    parent=$(lsblk -dnrpo PKNAME -- "$device")
    [[ "$parent" == "$expected_parent" ]] ||
        die "$device no longer belongs to $expected_parent"
    actual_number=$(lsblk -dnro PARTN -- "$device")
    actual_uuid=$(lsblk -dnro PARTUUID -- "$device")
    actual_start=$(lsblk -dnro START -- "$device")
    actual_size=$(blockdev --getsize64 "$device")
    actual_type=$(lsblk -dnro PARTTYPE -- "$device")
    [[ "$actual_number" == "$expected_number" ]] || die "Partition number changed for $device"
    [[ "$actual_start" == "$expected_start" ]] || die "Start sector changed for $device"
    [[ "$actual_size" == "$expected_size" ]] || die "Partition size changed for $device"
    [[ -n "$expected_uuid" && "${actual_uuid,,}" == "${expected_uuid,,}" ]] ||
        die "PARTUUID changed for $device"
    [[ -n "$expected_type" && "${actual_type,,}" == "${expected_type,,}" ]] ||
        die "GPT partition type changed for $device"
    ensure_node_idle "$device"
}

# Verify the geometry of a partition created by this script. / 校验本脚本新建分区的几何信息。
verify_created_partition() {
    local device=$1 expected_number=$2 usage=$3 planned_size=$4 disk_index=$5
    local target_disk=${INSTALL_DISKS[disk_index]} storage_mode=${DISK_MODES[disk_index]}
    local parent actual_number actual_type actual_uuid expected_type disk_pttype
    local actual_start actual_start_bytes actual_size expected_mib expected_size
    local minimum_size remaining_bytes previous_device='' previous_start previous_size
    local previous_end gap candidate
    [[ -b "$device" ]] || die "Created partition is missing: $device"
    parent=$(lsblk -dnrpo PKNAME -- "$device")
    actual_number=$(lsblk -dnro PARTN -- "$device")
    actual_type=$(lsblk -dnro PARTTYPE -- "$device")
    actual_uuid=$(lsblk -dnro PARTUUID -- "$device")
    actual_start=$(lsblk -dnro START -- "$device")
    actual_size=$(blockdev --getsize64 "$device")
    disk_pttype=$(lsblk -dnro PTTYPE -- "$target_disk")
    [[ "$parent" == "$target_disk" && "$actual_number" == "$expected_number" ]] ||
        die "Created partition identity mismatch for $device"
    [[ "${disk_pttype,,}" == gpt && -n "$actual_uuid" ]] ||
        die "Created partition has no stable GPT identity: $device"
    case "$storage_mode:$usage" in
        auto-data:*) expected_type='0fc63daf-8483-4772-8e79-3d69d8477de4' ;;
        *:boot) expected_type='c12a7328-f81f-11d2-ba4b-00a0c93ec93b' ;;
        *:swap) expected_type='0657fd6d-a4ab-43c4-84e5-0933c84b4f4f' ;;
        *) expected_type='0fc63daf-8483-4772-8e79-3d69d8477de4' ;;
    esac
    [[ "${actual_type,,}" == "$expected_type" ]] || die "Unexpected GPT type for $device"
    [[ "$actual_start" =~ ^[0-9]+$ ]] ||
        die "Invalid start geometry for $device"
    actual_start_bytes=$((actual_start * 512))
    if [[ "$expected_number" -eq 1 ]]; then
        (( actual_start_bytes > 0 && actual_start_bytes <= 64 * 1048576 )) ||
            die "Unexpected first-partition offset for $device"
    else
        for candidate in "${!PART_NUMBERS[@]}"; do
            if [[ "${PART_DISK_INDEXES[candidate]}" == "$disk_index" &&
                  "${PART_NUMBERS[candidate]}" -eq $((expected_number - 1)) ]]; then
                previous_device=${PART_DEVICES[candidate]}
                break
            fi
        done
        [[ -n "$previous_device" && -b "$previous_device" ]] ||
            die "Cannot verify the partition preceding $device"
        previous_start=$(lsblk -dnro START -- "$previous_device")
        previous_size=$(blockdev --getsize64 "$previous_device")
        previous_end=$((previous_start * 512 + previous_size))
        gap=$((actual_start_bytes - previous_end))
        (( gap >= 0 && gap <= 64 * 1048576 )) ||
            die "Unexpected partition gap before $device"
    fi
    if [[ "$storage_mode" == auto-data ]]; then
        expected_mib=0
    else
        case "$usage" in
            boot) expected_mib=${DISK_EFI_SIZE_MIB[disk_index]} ;;
            root) [[ "$storage_mode" == auto-root-only ]] && expected_mib=0 || expected_mib=${DISK_ROOT_SIZE_MIB[disk_index]} ;;
            home) expected_mib=${DISK_HOME_SIZE_MIB[disk_index]} ;;
            swap) expected_mib=${DISK_SWAP_SIZE_MIB[disk_index]} ;;
            *) expected_mib=0 ;;
        esac
    fi
    if (( expected_mib > 0 )); then
        expected_size=$((expected_mib * 1048576))
        [[ "$actual_size" == "$expected_size" ]] || die "Unexpected size for $device"
    else
        minimum_size=$((planned_size - 64 * 1048576))
        remaining_bytes=$((${DISK_SIZES[disk_index]} - actual_start_bytes - actual_size))
        (( actual_size >= minimum_size && actual_size <= planned_size &&
           remaining_bytes >= 0 && remaining_bytes <= 64 * 1048576 )) ||
            die "Unexpected fill-to-end size for $device"
    fi
    ensure_node_idle "$device"
}

# Revalidate every disk and actionable partition immediately before writes. / 写入前立即重新校验每块磁盘和待处理分区。
verify_storage_state() {
    local index disk_index disk mode node mounted_target action filesystem actual actual_uuid mount_targets disk_nodes
    [[ -d "$TARGET_ROOT" && ! -L "$TARGET_ROOT" ]] ||
        die "Target mountpoint changed or became a symlink: $TARGET_ROOT"
    verify_disk_identities
    mount_targets=$(findmnt -rn -o TARGET) || die 'Cannot inspect active mounts.'
    while IFS= read -r mounted_target; do
        case "$mounted_target" in
            "$TARGET_ROOT"|"$TARGET_ROOT"/*) die "$mounted_target is mounted below $TARGET_ROOT" ;;
        esac
    done <<<"$mount_targets"
    for ((disk_index=0; disk_index<${#INSTALL_DISKS[@]}; ++disk_index)); do
        disk=${INSTALL_DISKS[disk_index]}
        mode=${DISK_MODES[disk_index]}
        if [[ "$mode" == existing ]]; then
            ensure_node_idle "$disk"
        else
            disk_nodes=$(lsblk -nrpo NAME -- "$disk") ||
                die "Cannot enumerate installation-disk nodes: $disk"
            while IFS= read -r node; do
                [[ -n "$node" ]] && ensure_node_idle "$node"
            done <<<"$disk_nodes"
        fi
    done
    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do
        disk_index=${PART_DISK_INDEXES[index]}
        if [[ "${DISK_MODES[disk_index]}" == existing ]]; then
            verify_existing_partition "${PART_DEVICES[index]}" \
                "${INSTALL_DISKS[disk_index]}" "${PART_NUMBERS[index]}" \
                "${PART_UUIDS[index]}" "${PART_START_SECTORS[index]}" \
                "${PART_SIZES[index]}" "${PART_TYPES[index]}"
        fi
    done
    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do
        action=${PART_ACTIONS[index]}
        filesystem=${PART_FILESYSTEMS[index]}
        [[ "$action" == keep ]] || continue
        actual=$(blkid -s TYPE -o value -- "${PART_DEVICES[index]}") ||
            die "Cannot identify the kept filesystem on ${PART_DEVICES[index]}"
        actual=$(normalize_fs "$actual")
        [[ "$actual" == "$filesystem" ]] ||
            die "Kept filesystem changed on ${PART_DEVICES[index]} (expected $filesystem, got ${actual:-none})"
        actual_uuid=$(blkid -s UUID -o value -- "${PART_DEVICES[index]}") ||
            die "Cannot identify the kept filesystem UUID on ${PART_DEVICES[index]}"
        [[ -n "${PART_FS_UUIDS[index]}" &&
           "${actual_uuid,,}" == "${PART_FS_UUIDS[index],,}" ]] ||
            die "Filesystem UUID changed on ${PART_DEVICES[index]}"
    done
}
