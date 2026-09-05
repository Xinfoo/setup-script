# =============================================================================
# Target-visible local package mirror / 目标系统可见的本地软件包镜像
# =============================================================================

setup_target_local_mirror() {
    local mount_status path source uuid filesystem options option
    [[ "$USE_LOCAL_MIRROR" == true ]] || return 0
    for path in "$TARGET_ROOT/var" "$TARGET_ROOT/var/cache" "$TARGET_LOCAL_MIRROR"; do
        [[ ! -L "$path" ]] || die "Target local-mirror path is a symlink: $path"
    done
    mkdir -p -- "$TARGET_LOCAL_MIRROR"
    if findmnt -rn --mountpoint "$TARGET_LOCAL_MIRROR" >/dev/null 2>&1; then
        die "$TARGET_LOCAL_MIRROR is already a mountpoint."
    else
        mount_status=$?
        [[ "$mount_status" -eq 1 ]] || die 'Cannot inspect the target local-mirror mountpoint.'
    fi
    TARGET_LOCAL_MIRROR_MOUNTED=true
    mount --bind -- /run/media/root/F2FS-DATA/repo/archlinux "$TARGET_LOCAL_MIRROR"
    mount -o remount,bind,ro,nodev,nosuid,noexec -- "$TARGET_LOCAL_MIRROR"
    source=$(findmnt -rn --mountpoint "$TARGET_LOCAL_MIRROR" -o SOURCE) ||
        die 'Cannot identify the target local-mirror source.'
    uuid=$(findmnt -rn --mountpoint "$TARGET_LOCAL_MIRROR" -o UUID) ||
        die 'Cannot identify the target local-mirror UUID.'
    filesystem=$(findmnt -rn --mountpoint "$TARGET_LOCAL_MIRROR" -o FSTYPE) ||
        die 'Cannot identify the target local-mirror filesystem.'
    options=$(findmnt -rn --mountpoint "$TARGET_LOCAL_MIRROR" -o OPTIONS) ||
        die 'Cannot identify the target local-mirror options.'
    [[ "$source" == "$LOCAL_MIRROR_SOURCE" ||
       "$source" == "$LOCAL_MIRROR_SOURCE["* ]] ||
        die 'The target local mirror has an unexpected source.'
    [[ "${uuid,,}" == "${LOCAL_MIRROR_UUID,,}" && "${filesystem,,}" == f2fs ]] ||
        die 'The target local-mirror identity is incorrect.'
    for option in ro nodev nosuid noexec; do
        [[ ",$options," == *,"$option",* ]] ||
            die "The target local mirror is missing mount option: $option"
    done
}

# Remove only the temporary bind mount owned by this installer. / 只卸载本安装器创建的临时绑定挂载。
unmount_target_local_mirror() {
    local uuid filesystem
    [[ "$TARGET_LOCAL_MIRROR_MOUNTED" == true ]] || return 0
    uuid=$(findmnt -rn --mountpoint "$TARGET_LOCAL_MIRROR" -o UUID) ||
        die 'Cannot recheck the target local-mirror UUID before unmounting.'
    filesystem=$(findmnt -rn --mountpoint "$TARGET_LOCAL_MIRROR" -o FSTYPE) ||
        die 'Cannot recheck the target local-mirror filesystem before unmounting.'
    [[ "${uuid,,}" == "${LOCAL_MIRROR_UUID,,}" && "${filesystem,,}" == f2fs ]] ||
        die 'Refusing to unmount an unexpected target local mirror.'
    umount -- "$TARGET_LOCAL_MIRROR"
    TARGET_LOCAL_MIRROR_MOUNTED=false
    rmdir -- "$TARGET_LOCAL_MIRROR"
}
