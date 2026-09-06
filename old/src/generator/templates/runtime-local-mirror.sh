# =============================================================================
# Temporary local package mirror discovery / 临时本地软件包镜像探测
# =============================================================================

select_local_mirror_source() {
    local sources=() source_text source_status ancestors filesystem parent_type disk
    # The legacy mirror contract requires one uniquely labelled partition. / 旧版镜像约定要求恰好一个具有指定卷标的分区。
    if source_text=$(blkid -t LABEL=F2FS-DATA -o device); then
        [[ -z "$source_text" ]] || mapfile -t sources <<<"$source_text"
    else
        source_status=$?
        [[ "$source_status" -eq 2 ]] || die 'Cannot inspect local-mirror labels.'
    fi
    [[ "${#sources[@]}" -eq 1 ]] ||
        die "Expected exactly one F2FS-DATA partition, found ${#sources[@]}"
    # Record filesystem identity and the physical parent for later rechecks. / 记录文件系统身份和物理父盘，供后续复核。
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
    # Reject a source whose ancestry intersects any installation disk. / 拒绝祖先链与任何安装盘相交的镜像源。
    ancestors=$(lsblk -snrpo NAME -- "$LOCAL_MIRROR_SOURCE") ||
        die 'Cannot inspect the local-mirror device ancestry.'
    for disk in "${INSTALL_DISKS[@]}"; do
        if grep -Fxq -- "$disk" <<<"$ancestors"; then
            die 'The local mirror cannot reside on an installation disk.'
        fi
    done
    ensure_node_idle "$LOCAL_MIRROR_SOURCE"
}

# Recheck the complete mirror identity before mounting it. / 挂载前重新核对镜像的完整身份。
verify_local_mirror_identity() {
    local uuid parent size label filesystem serial
    [[ "$USE_LOCAL_MIRROR" == true ]] || return 0
    # Re-read every captured field instead of trusting an earlier device name. / 重新读取全部已记录字段，不信任先前的设备名。
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
    # All fields must still describe the exact source approved by the operator. / 所有字段必须仍指向操作者确认的同一来源。
    [[ "$uuid" == "$LOCAL_MIRROR_UUID" && "$label" == F2FS-DATA &&
       "${filesystem,,}" == f2fs && "$parent" == "$LOCAL_MIRROR_PARENT" &&
       "$size" == "$LOCAL_MIRROR_SIZE" &&
       "$serial" == "$LOCAL_MIRROR_PARENT_SERIAL" ]] ||
        die 'The local mirror identity changed after confirmation.'
    ensure_node_idle "$LOCAL_MIRROR_SOURCE"
}
