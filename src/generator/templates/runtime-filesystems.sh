# =============================================================================
# Filesystem preparation and mounting / 文件系统准备与挂载
# =============================================================================

format_partitions() {
    local index device action filesystem actual actual_uuid
    phase 'Applying filesystem actions'
    # KEEP performs identity checks only; every other action recreates the filesystem. / KEEP 仅执行身份检查；其他操作都会重建文件系统。
    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do
        device=${PART_DEVICES[index]}
        action=${PART_ACTIONS[index]}
        filesystem=${PART_FILESYSTEMS[index]}
        if [[ "$action" == keep ]]; then
            # Recheck both type and UUID immediately before any sibling is formatted. / 在格式化任何同级分区前再次核对类型和 UUID。
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
        # Dispatch to the formatter selected by the normalized plan filesystem. / 根据计划中规范化的文件系统调用对应格式化工具。
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
    local index device usage filesystem profile mountpoint destination options
    phase 'Mounting filesystems in path order'
    # Skip swap and format-only records; the emitted order places parents before children. / 跳过 Swap 和仅格式化记录；生成顺序保证父挂载先于子挂载。
    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do
        device=${PART_DEVICES[index]}
        usage=${PART_USAGES[index]}
        [[ "$usage" != swap && "$usage" != unused ]] || continue
        filesystem=${PART_FILESYSTEMS[index]}
        profile=${PART_MOUNT_PROFILES[index]}
        mountpoint=${PART_MOUNTPOINTS[index]}
        if [[ "$mountpoint" == / ]]; then
            # Mark root ownership before later mounts make the tree recursive. / 在后续挂载形成递归树前记录根挂载归属。
            destination=$TARGET_ROOT
            TARGET_MOUNTED=true
        else
            destination=$TARGET_ROOT$mountpoint
            [[ ! -L "$destination" ]] || die "Mountpoint must not be a symlink: $destination"
            mkdir -p -- "$destination"
        fi
        # Translate the filesystem/profile pair; currently only F2FS has non-default options. / 按文件系统与配置档组合转换；目前只有 F2FS 提供非默认参数。
        options=''
        case "$filesystem:$profile" in
            ext4:default|xfs:default|vfat:default|f2fs:default) ;;
            f2fs:balanced) options='noatime,lazytime,gc_merge,atgc,nodiscard,fsync_mode=nobarrier' ;;
            f2fs:compressed) options='noatime,lazytime,gc_merge,atgc,nodiscard,fsync_mode=nobarrier,compress_algorithm=zstd:6,compress_chksum' ;;
            *) die "Unsupported mount profile $profile for $filesystem" ;;
        esac
        if [[ -n "$options" ]]; then
            # Pass the selected profile as one option word to avoid accidental splitting. / 将所选配置档作为单个选项参数传递，避免意外拆词。
            mount -o "$options" -- "$device" "$destination"
        else
            mount -- "$device" "$destination"
        fi
    done
    # A missing root mount makes every subsequent target write unsafe. / 根挂载缺失会使后续所有目标写入都不安全。
    [[ "$TARGET_MOUNTED" == true ]] || die 'The root filesystem was not mounted.'
    # Enable swap only after the complete directory tree is mounted. / 完整目录树挂载后才启用 Swap。
    for ((index=0; index<${#PART_DEVICES[@]}; ++index)); do
        [[ "${PART_USAGES[index]}" == swap ]] || continue
        # Register ownership before swapon so an immediate failure still reaches cleanup state. / 在 swapon 前登记归属，使紧随其后的失败仍能进入清理状态。
        SWAPS_TO_DISABLE+=("${PART_DEVICES[index]}")
        swapon -- "${PART_DEVICES[index]}"
    done
}

# Stop the temporary HTTP mirror without affecting unrelated nginx processes. / 停止临时 HTTP 镜像且不影响其他 nginx 进程。
stop_local_mirror_server() {
    local status=0
    [[ "$LOCAL_MIRROR_SERVER_RUNNING" == true ]] || return 0
    # Signal and reap only the nginx master process started by this installer. / 仅终止并回收本安装器启动的 nginx 主进程。
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
    # Recheck the source immediately before constructing the fixed mount path. / 构造固定挂载路径前立即复核来源。
    verify_local_mirror_identity
    for path in /run /run/media /run/media/root /run/media/root/F2FS-DATA; do
        [[ ! -L "$path" ]] || die "Local mirror path component is a symlink: $path"
    done
    # Do not reuse a mountpoint owned by another process or earlier run. / 不复用其他进程或先前运行持有的挂载点。
    if findmnt -rn --mountpoint /run/media/root/F2FS-DATA >/dev/null 2>&1; then
        die '/run/media/root/F2FS-DATA is already a mountpoint.'
    else
        mount_status=$?
        [[ "$mount_status" -eq 1 ]] || die 'Cannot inspect the local mirror mountpoint.'
    fi
    mkdir -p -- /run/media/root/F2FS-DATA
    # Recheck path components after mkdir to close the creation-time substitution window. / mkdir 后再次检查路径组件，缩小创建期间的替换窗口。
    for path in /run /run/media /run/media/root /run/media/root/F2FS-DATA; do
        [[ ! -L "$path" ]] || die "Local mirror path component became a symlink: $path"
    done
    LOCAL_MIRROR_MOUNTED=true
    # The repository source remains read-only and cannot execute files. / 仓库来源保持只读且不可执行文件。
    mount -o ro,nodev,nosuid,noexec -- "$LOCAL_MIRROR_SOURCE" /run/media/root/F2FS-DATA
    [[ "$(findmnt -rn --mountpoint /run/media/root/F2FS-DATA -o SOURCE)" == \
       "$LOCAL_MIRROR_SOURCE" ]] || die 'The local mirror mounted from an unexpected source.'
    [[ -d /run/media/root/F2FS-DATA/repo/archlinux ]] ||
        die 'The F2FS-DATA partition does not contain repo/archlinux.'
    # Save the Live Pacman configuration before the one-time nginx bootstrap. / 一次性 nginx 引导前保存 Live 环境的 Pacman 配置。
    cp -a /etc/pacman.conf "$WORK_DIR/host-pacman.conf"
    cp -a /etc/pacman.d/mirrorlist "$WORK_DIR/host-mirrorlist"
    HOST_PACMAN_CHANGED=true
    # Setting the flag after both copies makes cleanup depend on a complete backup pair. / 两份备份都完成后才设置标志，使清理只依赖完整备份对。
    # Only this file:// bootstrap runs with signature checks disabled. / 只有本次 file:// 引导会关闭签名检查。
    sed -i -E 's/^[[:space:]#]*SigLevel[[:space:]]*=.*/SigLevel = Never/' /etc/pacman.conf
    printf '%s\n' 'Server = file:///run/media/root/F2FS-DATA/repo/archlinux/$repo/os/$arch' > /etc/pacman.d/mirrorlist
    pacman -Syy --noconfirm
    (( ${#LOCAL_MIRROR_LIVE_PACKAGES[@]} > 0 )) ||
        die 'The local-mirror Live package group is empty.'
    phase 'Installing the temporary local mirror server'
    pacman -S --needed --noconfirm "${LOCAL_MIRROR_LIVE_PACKAGES[@]}"
    require_command nginx
    # Use an isolated nginx configuration rather than the system service. / 使用隔离的 nginx 配置而非系统服务。
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
    # Refuse to displace an existing listener on the fixed loopback port. / 拒绝占用已有进程使用的固定回环端口。
    if (exec 9<>/dev/tcp/127.0.0.1/2304) 2>/dev/null; then
        die 'TCP port 127.0.0.1:2304 is already in use.'
    fi
    nginx -t -q -c "$WORK_DIR/local-mirror-nginx.conf" \
        -g "pid $WORK_DIR/local-mirror-nginx.pid;"
    # Foreground mode gives this script a concrete child PID without involving systemd. / 前台模式让脚本直接持有子进程 PID，不依赖 systemd。
    nginx -c "$WORK_DIR/local-mirror-nginx.conf" \
        -g "daemon off; pid $WORK_DIR/local-mirror-nginx.pid;" &
    # Record the child before readiness polling so cleanup can always reap it. / 就绪轮询前记录子进程，确保清理始终可以回收它。
    LOCAL_MIRROR_SERVER_PID=$!
    LOCAL_MIRROR_SERVER_RUNNING=true
    # Bound startup waiting to five seconds and notice early process death. / 将启动等待限制为五秒，并检测进程提前退出。
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
