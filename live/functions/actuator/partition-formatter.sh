#!/usr/bin/env bash

# 分区格式化器
partition_formatter() {
    local file_system
    local partition_dev_path

    for partition_dev_path in "${!file_system_choices[@]}"; do

        # 从关联数组中获取所要格式化的文件系统
        file_system="${file_system_choices["$partition_dev_path"]}"

        # 格式化文件系统
        case "$file_system" in
            "vfat")
                mkfs.fat -F 32 "$file_system_dev_path"
                ;;
            "ext4")
                mkfs.ext4 -F "$file_system_dev_path"
                ;;
            "xfs")
                mkfs.xfs -f "$file_system_dev_path"
                ;;
            "f2fs")
                mkfs.f2fs -f -O extra_attr,inode_checksum,sb_checksum,compression "$file_system_dev_path"
                ;;
            "swap")
                mkswap -f "$file_system_dev_path"
                ;;
        esac
    done
}
