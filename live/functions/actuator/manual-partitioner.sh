#!/usr/bin/env bash

# 手动分区器

manual_partitioner() {

    # 检查输入是否为空
    if [[ -z "${1:-}" ]]; then
        return 1
    fi

    local disk="$1"
    local main_disk="no"
    local partition

    # 检查是否传递--main参数
    if [[ "$1" == "--main" ]]; then
        if [[ -z "${2:-}" ]]; then
            return 1
        fi
        disk="$2"
        main_disk="yes"
    fi

    # 分区前的最后警告
    echo "Next, you will manually partition the selected disk; cfdisk will be launched. You must ensure the partitioning is complete before exiting. " >&2
    if confirm "Do you wish to proceed?"; then
        cfdisk "$disk"
    else
        return 1
    fi

    # 当磁盘为主磁盘时的处理
    if [[ "$main_disk" == "yes" ]]; then
        # 根分区
        clear
        echo "Please select a partition as the root(/) partition." >&2
        parition="$(partition_selector "$disk")" || return 1
        echo "Select a file system for the root partition." >&2
        file_system_setter --noswap "$partition"

        # EFI分区
        clear
        echo "Please select a partition as the EFI(/boot) partition."
        parition="$(partition_selector "$disk")" || return 1
        file_system_setter --efi "$partition"
    fi

    if confirm "Do you want to configure mount points for other partitions or enable a swap partition?"; then
        while true; do
            clear
            echo "Please select the partition to assign a mount point to."
            partition="$(partition_selector "$disk")" || return 1
            file_system_setter "$partition"
            if [[ "${file_system_chocies["$partition"]}" != "swap" ]]; then
                mount_point_setter "$partition" || continue
                if confirm "Do you want to continue assigning mount points for the partitions or enabling the swap partition?"; then
                    continue
                else
                    break
                fi
            fi
        done
    fi

}
