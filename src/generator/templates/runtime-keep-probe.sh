# =============================================================================
# Read-only probing of preserved filesystems / 保留文件系统的只读探测
# =============================================================================

probe_kept_filesystems() {
    local index device filesystem options mounted_source has_keep=false
    for ((index=0; index<${#PART_ACTIONS[@]}; ++index)); do
        [[ "${PART_ACTIONS[index]}" != keep ]] || has_keep=true
    done
    [[ "$has_keep" == true ]] || return 0
    KEEP_PROBE_MOUNT=$WORK_DIR/keep-probe
    install -d -m 0700 "$KEEP_PROBE_MOUNT"
    phase 'Read-only probing of filesystems marked KEEP'
    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do
        [[ "${PART_ACTIONS[index]}" == keep ]] || continue
        device=${PART_DEVICES[index]}
        filesystem=${PART_FILESYSTEMS[index]}
        if [[ "$filesystem" == swap ]]; then
            require_command swaplabel
            swaplabel "$device" >/dev/null ||
                die "The kept swap header cannot be read: $device"
            continue
        fi
        case "$filesystem" in
            vfat) options='ro,nodev,nosuid,noexec' ;;
            ext4) options='ro,noload,nodev,nosuid,noexec' ;;
            xfs) options='ro,norecovery,nodev,nosuid,noexec' ;;
            f2fs) options='ro,disable_roll_forward,nodev,nosuid,noexec' ;;
            *) die "Cannot probe unsupported kept filesystem: $filesystem" ;;
        esac
        KEEP_PROBE_SOURCE=$device
        KEEP_PROBE_ACTIVE=true
        if ! mount -t "$filesystem" -o "$options" -- "$device" "$KEEP_PROBE_MOUNT"; then
            die "The kept filesystem cannot be mounted read-only: $device"
        fi
        mounted_source=$(findmnt -rn --mountpoint "$KEEP_PROBE_MOUNT" -o SOURCE) ||
            die "Cannot verify the KEEP probe mount for $device"
        [[ "$mounted_source" == "$device" ]] ||
            die "The KEEP probe mounted an unexpected source for $device"
        umount -- "$KEEP_PROBE_MOUNT" || die "Cannot unmount the KEEP probe for $device"
        KEEP_PROBE_ACTIVE=false
        KEEP_PROBE_SOURCE=''
    done
    rmdir -- "$KEEP_PROBE_MOUNT" || die 'Cannot remove the KEEP probe directory.'
    KEEP_PROBE_MOUNT=''
}
