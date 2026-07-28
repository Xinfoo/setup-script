#!/usr/bin/env bash

# 硬盘选择器
disk_selector() {
    # 定义要使用的变量
    local disk disk_dev_path choice
    local disks
    local PS3="Select a disk: "
    local -a disk_dev_path_list=()

    # 循环处理列表为数组并添加全路径
    disks="$(disk_detector)" || return 1
    for disk in $disks; do
        disk_dev_path="/dev/$disk"
        disk_dev_path_list+=("$disk_dev_path")
    done

    # 列出块设备信息
    lsblk -f -o NAME,FSTYPE,SIZE,MOUNTPOINT,FSUSED,FSAVAIL,FSUSE% "${disk_dev_path_list[@]}" >&2
    echo >&2

    # 选择块设备并把块设备全路径输出到标准输出
    select choice in ${disk_dev_path_list[@]}; do
        if [[ -n $choice ]]; then
            echo "$choice"
            return 0
        else
            echo "Invalid selection, please choose a number from the list." >&2
        fi
    done

    return 1
}
