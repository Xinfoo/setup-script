#!/usr/bin/env bash

# 挂载点挂载器
mounter() {
    if [[ -z "${1:-}" ]]; then
        return 1
    fi

    mount_detector "$1" || return 1

    local choice
    local file_system="${file_system_choices["$1"]}"
    local PS3="Select mount options: "
    local -a options=(
        "Optimized mount options(recommended)"
        "Default mount options")

    # 启动swap
    if [[ "$file_system" == "swap" ]]; then
        swapon "$1"
        return 0
    fi

    local mount_point="${mount_point_choices["$1"]}"

    # 针对F2FS的挂载选项特殊处理
    if [[ "$file_system" == "f2fs" ]]; then
        echo "What mount options would you like to enable for your $mount_point F2FS?" >&2
        select choice in "${options[@]}"; do
            if [[ "$mount_point" == "/" ]]; then
                case "$REPLY" in
                    1)
                        mount -o noatime,lazytime,atgc,nodiscard,fsync_mode=nobarrier "$1" "/mnt"
                        return 0
                        ;;
                    2)
                        mount "$1" "/mnt"
                        return 0
                        ;;
                esac
            else
                case "$REPLY" in
                    1)
                        mount --mkdir -o noatime,lazytime,atgc,nodiscard,fsync_mode=nobarrier "$1" "/mnt$mount_point"
                        return 0
                        ;;
                    2)
                        mount --mkdir "$1" "/mnt$mount_point"
                        return 0
                        ;;
                esac
            fi
        done
    fi

    # 挂载普通文件系统
    if [[ "$mount_point" == "/" ]]; then
        mount "$1" "/mnt"
    else
        mount --mkdir "$1" "/mnt$mount_point"
    fi
}
