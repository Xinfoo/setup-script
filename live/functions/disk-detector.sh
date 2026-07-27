#!/usr/bin/env bash

# 硬盘探测器
disk_detector() {
    # 定义要使用的变量和数组
    local disk disk_sys_dir
    local disk_list=()

    # 循环检测块设备，将可用设备加入数组
    for disk_sys_dir in /sys/block/*; do
        # 检查块设备目录
        if [[ ! -d "$disk_sys_dir" ]]; then
            continue
        fi

        # 检查是否是虚拟块设备
        if [[ ! -e "$disk_sys_dir/device" ]]; then
            continue
        fi

        # 检查是否是USB块设备
        if [[ "$(lsblk --noheadings --nodeps --raw --output TRAN "/dev/$(basename "$disk_sys_dir")")" == "usb" ]]; then
            continue
        fi

        # 检查是否是可移动设备
        if [[ -f "$disk_sys_dir/removable" ]] && [[ "$(cat "$disk_sys_dir/removable")" == "0" ]];then
            # 将扫描到的块设备全路径加入数组列表
            disk="$(basename "$disk_sys_dir")"
            disk_list+=("$disk")
        fi
    done

    # 检查数组是否为空
    if [[ "${#disk_list[@]}" == "0" ]];then
        echo "No usable disks available!" >&2
        return 1
    fi

    # 输出数组为列表
    echo "${disk_list[@]}"
}
