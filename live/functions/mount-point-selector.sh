#!/usr/bin/env bash

# 挂载点选择器
mount_point_selector() {
    local PS3="Select a mount point: "
    local -a mount_point_list=("/home" "/var" "/usr" "/opt")
    local -a temp_mount_point_list=()

    # 带有--root参数，直接挂载为根分区
    if [[ "$1" == "--root" ]]; then
        mount_point_choices["$2"]="/"
        return 0
    fi

    # 带有--efi参数，直接挂载为/boot分区
    if [[ "$1" == "--efi" ]]; then
        mount_point_choices["$2"]="/boot"
        return 0
    fi

    # 将已经使用过的挂载点移除
    for mounted_point in ${mount_point_choices[@]}; do
        temp_mount_point_list=()
        for mount_point in ${mount_point_list[@]}; do
            if [[ "$mount_point" != "$mounted_point" ]]; then
                temp_mount_point_list+=("$mount_point")
            fi
        done
        mount_point_list=("${temp_mount_point_list[@]}")
    done

    # 检查是否为空数组
    if [[ "${#mount_point_list[@]}" == "0" ]]; then
        echo "No mount point available!" >&2
        return 1
    fi

    if confirm "Do you want to select a mount point for this disk?"; then
        select choice in ${mount_point_list[@]}; do
            echo "You have chosen $choice" >&2
            if confirm "Are you sure you've made the right choice?"; then
                mount_point_choices["$1"]="$choice"
                break
            fi
        done
    fi
}
