#!/usr/bin/env bash

# 分区格式化器
partition_formatter() {
    # 从关联数组中获取所要格式化的文件系统
    local file_system="${file_system_choices["$1"]}"

    # 格式化文件系统
    case $file_system in
        "vfat")
            mkfs.fat -F 32 "$1"
            ;;
        "ext4")
            mkfs.ext4 -F "$1"
            ;;
        "xfs")
            mkfs.xfs -f "$1"
            ;;
        "f2fs")
            mkfs.f2fs -f -O extra_attr,inode_checksum,sb_checksum,compression "$1"
            ;;
        "swap")
            mkswap -f "$1"
            ;;
    esac
}
