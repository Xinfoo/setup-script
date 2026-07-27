#!/usr/bin/env bash

# 挂载点挂载器
mounter() {
    # 定义要使用的变量
    local mount_point="${mount_point_choices["$1"]}"
    local file_system="${file_system_choices["$1"]}"
    local PS3="Select mount options: "

    # 针对F2FS的挂载选项特殊处理
    if [[ "$file_system" == "f2fs" ]]; then
        echo "What mount options would you like to enable for your $mount_point F2FS?" >&2
        echo "1>Optimized mount options(recommended)" >&2
        echo "2>Default mount options" >&2
        select choice in Optimized Default; do
            if [[ "$mount_point" == "/" ]]; then
                case $choice in
                    Optimized)
                        mount -o noatime,lazytime,background_gc=off,atgc,nodiscard,fsync_mode=nobarrier "$1" "/mnt"
                        return 0
                        ;;
                    Default)
                        mount "$1" "/mnt"
                        return 0
                        ;;
                esac
            else
                case $choice in
                    Optimized)
                        mount --mkdir -o noatime,lazytime,background_gc=off,atgc,nodiscard,fsync_mode=nobarrier "$1" "/mnt$mount_point"
                        return 0
                        ;;
                    Default)
                        mount --mkdir "$1" "/mnt$mount_point"
                        return 0
                        ;;
                esac
            fi
        done
    fi

    # 启动swap
    if [[ "$file_system" == "swap" ]]; then
        swapon "$1"
        return 0
    fi

    # 挂载普通分区
    mount --mkdir "$1" "/mnt$mount_point"
}
