# =============================================================================
# Filesystem preparation and mounting / 文件系统准备与挂载
# =============================================================================

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

# Mount entries are emitted in path order; swap is enabled after mounts. / 挂载项按路径顺序生成；挂载完成后启用交换空间。
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

# Stop the temporary HTTP mirror without affecting unrelated nginx processes. / 停止临时 HTTP 镜像且不影响其他 nginx 进程。
stop_local_mirror_server() {
    local status=0
    [[ "$LOCAL_MIRROR_SERVER_RUNNING" == true ]] || return 0
    if kill -0 "$LOCAL_MIRROR_SERVER_PID" 2>/dev/null; then
        kill -TERM "$LOCAL_MIRROR_SERVER_PID" 2>/dev/null || status=1
    fi
    wait "$LOCAL_MIRROR_SERVER_PID" 2>/dev/null || status=1
    LOCAL_MIRROR_SERVER_RUNNING=false
    LOCAL_MIRROR_SERVER_PID=''
    return "$status"
}

# Mount the repository and bootstrap the loopback HTTP mirror. / 挂载仓库并引导仅监听回环地址的 HTTP 镜像。
setup_local_mirror() {
    local attempt mount_status path server_ready=false
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
    (( ${#LOCAL_MIRROR_LIVE_PACKAGES[@]} > 0 )) ||
        die 'The local-mirror Live package group is empty.'
    phase 'Installing the temporary local mirror server'
    pacman -S --needed --noconfirm "${LOCAL_MIRROR_LIVE_PACKAGES[@]}"
    require_command nginx
    cat > "$WORK_DIR/local-mirror-nginx.conf" <<'NGINX_CONFIG'
worker_processes 1;
error_log stderr notice;
events {
    worker_connections 128;
}
http {
    access_log off;
    sendfile on;
    server {
        listen 127.0.0.1:2304;
        server_name localhost;
        root /run/media/root/F2FS-DATA/repo/archlinux;
        location / {
            try_files $uri $uri/ =404;
        }
    }
}
NGINX_CONFIG
    if (exec 9<>/dev/tcp/127.0.0.1/2304) 2>/dev/null; then
        die 'TCP port 127.0.0.1:2304 is already in use.'
    fi
    nginx -t -q -c "$WORK_DIR/local-mirror-nginx.conf" \
        -g "pid $WORK_DIR/local-mirror-nginx.pid;"
    nginx -c "$WORK_DIR/local-mirror-nginx.conf" \
        -g "daemon off; pid $WORK_DIR/local-mirror-nginx.pid;" &
    LOCAL_MIRROR_SERVER_PID=$!
    LOCAL_MIRROR_SERVER_RUNNING=true
    for ((attempt=0; attempt<50; ++attempt)); do
        if (exec 9<>/dev/tcp/127.0.0.1/2304) 2>/dev/null; then
            server_ready=true
            break
        fi
        if ! kill -0 "$LOCAL_MIRROR_SERVER_PID" 2>/dev/null; then
            wait "$LOCAL_MIRROR_SERVER_PID" 2>/dev/null || true
            LOCAL_MIRROR_SERVER_RUNNING=false
            LOCAL_MIRROR_SERVER_PID=''
            die 'The temporary local mirror server exited during startup.'
        fi
        sleep 0.1
    done
    [[ "$server_ready" == true ]] || die 'The temporary local mirror server did not become ready.'

    # Only the nginx bootstrap bypasses signatures; HTTP operations use the original policy. / 仅 nginx 引导跳过验签；HTTP 阶段恢复原策略。
    cp -a -- "$WORK_DIR/host-pacman.conf" /etc/pacman.conf
    printf '%s\n' 'Server = http://127.0.0.1:2304/$repo/os/$arch' > /etc/pacman.d/mirrorlist
    pacman -Syy --noconfirm
}
