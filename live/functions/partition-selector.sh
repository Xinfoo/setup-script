#!/usr/bin/env bash

# 分区选择器
partition_selector() {
    # 定义要使用的变量和数组
    local disk disks disk_dev_path choice
    local partition_sys_dir partition_dev_path
    local PS3="Select a partition: "
    local -a disk_dev_path_list=()
    local -a partition_dev_path_list=()

    # 循环处理分区列表数组
    disks="$(disk_detector)" || return 1
    for disk in $disks; do
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
