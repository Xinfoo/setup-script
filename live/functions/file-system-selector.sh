#!/usr/bin/env bash

# 文件系统选择器
file_system_selector() {
    # 定义要使用的变量和数组
    local PS3="Select the file system you want to format: "
    local -a file_system_list=("Ext4" "XFS" "F2FS" "SWAP")

    # 检测是否传递EFI参数
    if [[ "$1" == "--efi" ]]; then
        file_system_choices["$2"]="vfat"
        return 0
    fi

    # 选择要格式化的分区并关联数组
    select choice in ${file_system_list[@]}; do
        case $choice in
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
