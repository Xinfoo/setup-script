select_local_mirror_source() {
    local sources=() source_text source_status ancestors filesystem parent_type disk
    if source_text=$(blkid -t LABEL=F2FS-DATA -o device); then
        [[ -z "$source_text" ]] || mapfile -t sources <<<"$source_text"
    else
        source_status=$?
        [[ "$source_status" -eq 2 ]] || die 'Cannot inspect local-mirror labels.'
    fi
    [[ "${#sources[@]}" -eq 1 ]] ||
        die "Expected exactly one F2FS-DATA partition, found ${#sources[@]}"
    LOCAL_MIRROR_SOURCE=${sources[0]}
    [[ -b "$LOCAL_MIRROR_SOURCE" ]] || die 'The local mirror source is not a block device.'
    filesystem=$(blkid -s TYPE -o value -- "$LOCAL_MIRROR_SOURCE") ||
        die 'Cannot identify the local mirror filesystem.'
    [[ "${filesystem,,}" == f2fs ]] || die 'The F2FS-DATA source must use F2FS.'
    LOCAL_MIRROR_UUID=$(blkid -s UUID -o value -- "$LOCAL_MIRROR_SOURCE") ||
        die 'Cannot identify the local mirror UUID.'
    [[ -n "$LOCAL_MIRROR_UUID" ]] || die 'The local mirror has no filesystem UUID.'
    LOCAL_MIRROR_PARENT=$(lsblk -dnrpo PKNAME -- "$LOCAL_MIRROR_SOURCE") ||
        die 'Cannot identify the local mirror parent disk.'
    [[ -n "$LOCAL_MIRROR_PARENT" ]] || die 'The local mirror must be a disk partition.'
    parent_type=$(lsblk -dnro TYPE -- "$LOCAL_MIRROR_PARENT") ||
        die 'Cannot inspect the local mirror parent disk.'
    [[ "$parent_type" == disk ]] || die 'The local mirror parent is not a whole disk.'
    LOCAL_MIRROR_PARENT_SERIAL=$(lsblk -dno SERIAL -- "$LOCAL_MIRROR_PARENT" |
        sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
    LOCAL_MIRROR_SIZE=$(blockdev --getsize64 "$LOCAL_MIRROR_SOURCE") ||
        die 'Cannot determine the local mirror size.'
    ancestors=$(lsblk -snrpo NAME -- "$LOCAL_MIRROR_SOURCE") ||
        die 'Cannot inspect the local-mirror device ancestry.'
    for disk in "${INSTALL_DISKS[@]}"; do
        if grep -Fxq -- "$disk" <<<"$ancestors"; then
            die 'The local mirror cannot reside on an installation disk.'
        fi
    done
    ensure_node_idle "$LOCAL_MIRROR_SOURCE"
}

verify_local_mirror_identity() {
    local uuid parent size label filesystem serial
    [[ "$USE_LOCAL_MIRROR" == true ]] || return 0
    [[ -b "$LOCAL_MIRROR_SOURCE" ]] || die 'The selected local mirror disappeared.'
    uuid=$(blkid -s UUID -o value -- "$LOCAL_MIRROR_SOURCE") ||
        die 'Cannot recheck the local mirror UUID.'
    label=$(blkid -s LABEL -o value -- "$LOCAL_MIRROR_SOURCE") ||
        die 'Cannot recheck the local mirror label.'
    filesystem=$(blkid -s TYPE -o value -- "$LOCAL_MIRROR_SOURCE") ||
        die 'Cannot recheck the local mirror filesystem.'
    parent=$(lsblk -dnrpo PKNAME -- "$LOCAL_MIRROR_SOURCE") ||
        die 'Cannot recheck the local mirror parent.'
    size=$(blockdev --getsize64 "$LOCAL_MIRROR_SOURCE") ||
        die 'Cannot recheck the local mirror size.'
    serial=$(lsblk -dno SERIAL -- "$parent" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
    [[ "$uuid" == "$LOCAL_MIRROR_UUID" && "$label" == F2FS-DATA &&
       "${filesystem,,}" == f2fs && "$parent" == "$LOCAL_MIRROR_PARENT" &&
       "$size" == "$LOCAL_MIRROR_SIZE" &&
       "$serial" == "$LOCAL_MIRROR_PARENT_SERIAL" ]] ||
        die 'The local mirror identity changed after confirmation.'
    ensure_node_idle "$LOCAL_MIRROR_SOURCE"
}

