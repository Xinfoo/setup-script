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

    # 询问用户是否要擦除磁盘
    if confirm "Do you want to erase the disk?"; then
        disk_wiper "$disk"
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
        partition="$(partition_selector "$disk")" || return 1
        echo "Select a file system for the root partition." >&2
        file_system_setter --noswap "$partition"
        mount_point_setter --root "$partition"

        # EFI分区
        clear
        echo "Please select a partition as the EFI(/boot) partition."
        partition="$(partition_selector "$disk")" || return 1
        file_system_setter --efi "$partition"
        mount_point_setter --efi "$partition"
    fi

    if confirm "Do you want to configure mount points for other partitions or enable a swap partition?"; then
        while true; do
            clear
            echo "Please select the partition to assign a mount point to."
            partition="$(partition_selector "$disk")" || return 1
            file_system_setter "$partition"
            if [[ "${file_system_choices["$partition"]}" != "swap" ]]; then
                mount_point_setter "$partition" || continue
                if confirm "Do you want to continue assigning mount points for the partitions or enabling the swap partition?"; then
                    continue
                else
                    break
                fi
            fi
            if confirm "Do you want to continue assigning mount points for the partitions or enabling the swap partition?"; then
                continue
            else
                break
            fi
        done
    fi

    print_partition_table
    echo "Ensure that the new mount scheme does not contain duplicate mounts," >&2
    echo "and that there exists one and only one root partition and EFI partition." >&2
    if confirm "Are you sure you want this partitioning scheme?"; then
        return 0
    else
        file_system_choices=()
        mount_point_choices=()
        return 1
    fi
}
