#!/usr/bin/env bash

# 文件系统设置器
file_system_setter() {
    # 定义要使用的变量和数组
    local choice
    local partition
    local PS3="Select the file system you want to format: "
    local -a file_system_list=("Ext4" "XFS" "F2FS" "SWAP")

    if [[ -z "${1:-}" ]]; then
        return 1
    fi

    partition="$1"

    # 检测是否传递EFI参数
    if [[ "$1" == "--efi" ]]; then
        if [[ -z "${2:-}" ]]; then
            return 1
        fi

        partition="$2"
        file_system_choices["$partition"]="vfat"
        return 0
    fi

    # 检测是否传递swap参数
    if [[ "$1" == "--swap" ]]; then
        if [[ -z "${2:-}" ]]; then
            return 1
        fi

        partition="$2"
        file_system_choices["$partition"]="swap"
        return 0
    fi

    # 检测是否传递noswap参数
    if [[ "$1" == "--noswap" ]]; then
        if [[ -z "${2:-}" ]]; then
            return 1
        fi
        partition="$2"
        unset "file_system_list[4]"
    fi

    # 选择要格式化的分区并关联数组
    select choice in "${file_system_list[@]}"; do
        case "$choice" in
            "Ext4")
                file_system_choices["$1"]="ext4"
                return 0
                ;;
            "XFS")
                file_system_choices["$1"]="xfs"
                return 0
                ;;
            "F2FS")
                file_system_choices["$1"]="f2fs"
                return 0
                ;;
            "SWAP")
                file_system_choices["$1"]="swap"
                return 0
                ;;
        esac
    done
}
