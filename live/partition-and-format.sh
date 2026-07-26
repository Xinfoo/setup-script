#!/usr/bin/env bash
set -euo pipefail

permission_check() {
    if [[ "$EUID" -ne "0" ]]; then
        echo "Please run this script with root."
        exit 1
    fi
}

disk_selector() {
    # 定义要使用的变量和数组
    local disk
    local PS3="Select a disk: "
    local -a disk_list=()

    # 循环检测块设备，将可用设备加入数组
    for block_path in "/sys/block/"*; do
        # 检查块设备目录
        if [[ ! -d "$block_path" ]]; then
            continue
        fi

        # 检查是否是虚拟块设备
        if [[ ! -e "$block_path/device" ]]; then
            continue
        fi

        # 检查是否是USB块设备
        if [[ "$(lsblk --noheadings --nodeps --raw --output TRAN "/dev/$(basename "$block_path")")" == "usb" ]]; then
            continue
        fi

        # 检查是否是可移动设备
        if [[ -f "$block_path/removable" ]] && [[ "$(cat "$block_path/removable")" == "0" ]];then
            # 将扫描到的块设备全路径加入数组列表
            disk="/dev/$(basename "$block_path")"
            disk_list+=("$disk")
        fi
    done

    # 检查数组是否为空
    if [[ "${#disk_list[@]}" == "0" ]];then
        echo "No usable disks available!" >&2
        return 1
    fi

    # 列出块设备信息
    lsblk -f -o NAME,FSTYPE,SIZE,MOUNTPOINT,FSUSED,FSAVAIL,FSUSE% "${disk_list[@]}" >&2
    echo >&2

    # 选择块设备并把块设备全路径输出到标准输出
    select choice in "${disk_list[@]}"; do
        if [[ -n $choice ]]; then
            echo "$choice"
            return 0
        else
            echo "Invalid selection, please choose a number from the list." >&2
        fi
    done
    return 1
}

disk_wiper() {
    wipefs -a "$1"
    if [[ "$(lsblk --noheadings --nodeps --raw --output ROTA "$1")" == "0" ]] && [[ "$(lsblk --discard --noheadings --nodeps --raw --output DISC-GRAN "$1")" != "0B" ]]; then
        blkdiscard -f "$1"
    fi
}

disk_selector
