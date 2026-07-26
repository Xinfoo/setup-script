#!/usr/bin/env bash
set -euo pipefail

# 权限检查器
permission_check() {
    if [[ "$EUID" -ne "0" ]]; then
        echo "Please run this script with root."
        exit 1
    fi
}

# 硬盘探测器
disk_detector() {
    # 定义要使用的变量和数组
    local disk
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

# 挂载检查器
mount_detector() {
    if findmnt --source "$1"; then
        echo "This partition has already been mounted." >&2
        return 1
    else
        return 0
    fi
}

# 硬盘选择器
disk_selector() {
    # 定义要使用的变量
    local disk_dev_path
    local PS3="Select a disk: "
    local -a disk_dev_path_list=()

    # 循环处理列表为数组并添加全路径
    for disk in $(disk_detector); do
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

# 分区选择器
partition_selector() {
    # 定义要使用的变量和数组
    local disk_dev_path
    local partition_dev_path
    local PS3="Select a partition: "
    local -a disk_dev_path_list=()
    local -a partition_dev_path_list=()

    # 循环处理分区列表数组
    for disk in $(disk_detector); do
        # 检查磁盘是否有分区
        if [[ -z "$(ls "/sys/block/$disk" | grep "$disk")" ]]; then
            continue
        fi

        # 循环处理磁盘列表为数组并添加全路径
        disk_dev_path="/dev/$disk"
        disk_dev_path_list+=("$disk_dev_path")

        # 循环将磁盘下的分区加入数组
        for partition_sys_dir in /sys/block/$disk/$disk*; do
            partition_dev_path="/dev/$(basename "$partition_sys_dir")"
            partition_dev_path_list+=("$partition_dev_path")
        done
    done

    # 检查数组是否为空
    if [[ "${#partition_dev_path_list[@]}" == "0" ]];then
        echo "No usable partitions available!" >&2
        return 1
    fi

    # 列出块设备信息
    lsblk -f -o NAME,FSTYPE,SIZE,MOUNTPOINT,FSUSED,FSAVAIL,FSUSE% "${disk_dev_path_list[@]}" >&2
    echo >&2

    # 选择分区并把分区全路径输出到标准输出
    select choice in ${partition_dev_path_list[@]}; do
        if [[ -n $choice ]]; then
            echo "$choice"
            return 0
        else
            echo "Invalid selection, please choose a number from the list." >&2
        fi
    done

    return 1
}

# 硬盘擦除器
disk_wiper() {
    # 检查输入是否为空
    if [[ -z "$1" ]]; then
        return 1
    fi

    # 清除分区表
    wipefs -a "$1"

    # 如果目标为支持丢弃的SSD，则整盘清零
    if [[ "$(lsblk --noheadings --nodeps --raw --output ROTA "$1")" == "0" ]] && [[ "$(lsblk --discard --noheadings --nodeps --raw --output DISC-GRAN "$1")" != "0B" ]]; then
        blkdiscard -f "$1"
    fi
}
