format_partitions() {
    local index device action filesystem actual actual_uuid
    phase 'Applying filesystem actions'
    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do
        device=${PART_DEVICES[index]}
        action=${PART_ACTIONS[index]}
        filesystem=${PART_FILESYSTEMS[index]}
        if [[ "$action" == keep ]]; then
            actual=$(blkid -s TYPE -o value -- "$device") ||
                die "Cannot identify the kept filesystem on $device"
            actual=$(normalize_fs "$actual")
            [[ "$actual" == "$filesystem" ]] ||
                die "Kept filesystem changed on $device (expected $filesystem, got ${actual:-none})"
            actual_uuid=$(blkid -s UUID -o value -- "$device") ||
                die "Cannot identify the kept filesystem UUID on $device"
            [[ -n "${PART_FS_UUIDS[index]}" &&
               "${actual_uuid,,}" == "${PART_FS_UUIDS[index],,}" ]] ||
                die "Filesystem UUID changed on $device"
            continue
        fi
        printf 'Formatting %s as %s\n' "$device" "$filesystem"
        case "$filesystem" in
            vfat) mkfs.fat -F 32 "$device" ;;
            ext4) mkfs.ext4 -F "$device" ;;
            xfs) mkfs.xfs -f "$device" ;;
            f2fs) mkfs.f2fs -f -O extra_attr,inode_checksum,sb_checksum,compression "$device" ;;
            swap) mkswap --force "$device" ;;
            *) die "Unsupported filesystem: $filesystem" ;;
        esac
    done
}

mount_filesystems() {
    local index device usage filesystem mode mountpoint destination options
    phase 'Mounting filesystems in path order'
    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do
        device=${PART_DEVICES[index]}
        usage=${PART_USAGES[index]}
        [[ "$usage" != swap && "$usage" != unused ]] || continue
        filesystem=${PART_FILESYSTEMS[index]}
        mode=${PART_F2FS_MODES[index]}
        mountpoint=${PART_MOUNTPOINTS[index]}
        if [[ "$mountpoint" == / ]]; then
            destination=$TARGET_ROOT
            TARGET_MOUNTED=true
        else
            destination=$TARGET_ROOT$mountpoint
            [[ ! -L "$destination" ]] || die "Mountpoint must not be a symlink: $destination"
            mkdir -p -- "$destination"
        fi
        options=''
        if [[ "$filesystem" == f2fs ]]; then
            case "$mode" in
                balanced) options='noatime,lazytime,gc_merge,atgc,nodiscard,fsync_mode=nobarrier' ;;
                compressed) options='noatime,lazytime,gc_merge,atgc,nodiscard,fsync_mode=nobarrier,compress_algorithm=zstd:6,compress_chksum' ;;
            esac
        fi
        if [[ -n "$options" ]]; then
            mount -o "$options" -- "$device" "$destination"
        else
            mount -- "$device" "$destination"
        fi
    done
    [[ "$TARGET_MOUNTED" == true ]] || die 'The root filesystem was not mounted.'
    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do
        [[ "${PART_USAGES[index]}" == swap ]] || continue
        SWAPS_TO_DISABLE+=("${PART_DEVICES[index]}")
        swapon -- "${PART_DEVICES[index]}"
    done
}

setup_local_mirror() {
    local mount_status path
    [[ "$USE_LOCAL_MIRROR" == true ]] || return 0
    phase 'Mounting the confirmed local package mirror'
    verify_local_mirror_identity
    for path in /run /run/media /run/media/root /run/media/root/F2FS-DATA; do
        [[ ! -L "$path" ]] || die "Local mirror path component is a symlink: $path"
    done
    if findmnt -rn --mountpoint /run/media/root/F2FS-DATA >/dev/null 2>&1; then
        die '/run/media/root/F2FS-DATA is already a mountpoint.'
    else
        mount_status=$?
        [[ "$mount_status" -eq 1 ]] || die 'Cannot inspect the local mirror mountpoint.'
    fi
    mkdir -p -- /run/media/root/F2FS-DATA
    for path in /run /run/media /run/media/root /run/media/root/F2FS-DATA; do
        [[ ! -L "$path" ]] || die "Local mirror path component became a symlink: $path"
    done
    LOCAL_MIRROR_MOUNTED=true
    mount -o ro,nodev,nosuid,noexec -- "$LOCAL_MIRROR_SOURCE" /run/media/root/F2FS-DATA
    [[ "$(findmnt -rn --mountpoint /run/media/root/F2FS-DATA -o SOURCE)" == \
       "$LOCAL_MIRROR_SOURCE" ]] || die 'The local mirror mounted from an unexpected source.'
    [[ -d /run/media/root/F2FS-DATA/repo/archlinux ]] ||
        die 'The F2FS-DATA partition does not contain repo/archlinux.'
    cp -a /etc/pacman.conf "$WORK_DIR/host-pacman.conf"
    cp -a /etc/pacman.d/mirrorlist "$WORK_DIR/host-mirrorlist"
    HOST_PACMAN_CHANGED=true
    sed -i -E 's/^[[:space:]#]*SigLevel[[:space:]]*=.*/SigLevel = Never/' /etc/pacman.conf
    printf '%s\n' 'Server = file:///run/media/root/F2FS-DATA/repo/archlinux/$repo/os/$arch' > /etc/pacman.d/mirrorlist
    pacman -Syy --noconfirm
}

