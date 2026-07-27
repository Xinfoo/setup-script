#!/usr/bin/env bash

# 输出分区状态表
print_partition_table() {
    # 定义要使用的变量和数组
    local disk_dev_path
    local line disk part parent
    local -A real_fs real_mount real_size real_type
    local -a disk_dev_path_list=()
    local -a all_disks=()
    local size fs mount final_fs final_mount

    # 循环处理磁盘列表添加全路径
    for item in $(disk_detector); do
        disk_dev_path="/dev/$item"
        disk_dev_path_list+=("$disk_dev_path")
    done

    # 读取所有真实块设备信息
    while IFS= read -r line; do
        [[ -z "$line" ]] && continue
        eval "$line"
        real_fs["$NAME"]="$FSTYPE"
        real_mount["$NAME"]="$MOUNTPOINT"
        real_size["$NAME"]="$SIZE"
        real_type["$NAME"]="$TYPE"
        if [[ "$TYPE" == "disk" ]]; then
            all_disks+=("$NAME")
        fi
    done < <(lsblk --paths --pairs --output NAME,SIZE,FSTYPE,MOUNTPOINT,TYPE --noheadings "${disk_dev_path_list[@]}")

    if [[ ${#all_disks[@]} -eq 0 ]]; then
        echo "No disks found." >&2
        return
    fi

    # 表头
    printf "%-20s %10s %10s %s\n" "DEVICE" "SIZE" "FSTYPE" "MOUNTPOINT"
    printf "%-20s %10s %10s %s\n" "------" "----" "------" "----------"

    # 遍历每个磁盘
    for disk in "${all_disks[@]}"; do
        # 磁盘本身（顶格，宽度20，与表头对齐）
        size="${real_size[$disk]:--}"
        fs="${real_fs[$disk]:--}"
        mount="${real_mount[$disk]:--}"
        printf "%-20s %10s %10s %s\n" "$disk" "$size" "$fs" "$mount"

        # 收集该磁盘下的分区
        local -a parts=()
        # 真实分区
        for part in "${!real_type[@]}"; do
            if [[ "${real_type[$part]}" == "part" ]]; then
                parent="$(sed -E 's/p?[0-9]+$//' <<< "$part")"
                [[ "$parent" == "$disk" ]] && parts+=("$part")
            fi
        done
        # 预设中存在但系统当前不存在的分区
        for part in "${!file_system_choices[@]}" "${!mount_point_choices[@]}"; do
            if [[ -z "${real_size[$part]}" ]]; then
                parent="$(sed -E 's/p?[0-9]+$//' <<< "$part")"
                [[ "$parent" == "$disk" ]] && parts+=("$part")
            fi
        done

        # 去重并自然排序
        mapfile -t sorted_parts < <(printf '%s\n' "${parts[@]}" | sort -uV)
        local count=${#sorted_parts[@]}

        # 输出每个分区
        for (( i=0; i<count; i++ )); do
            part="${sorted_parts[$i]}"
            # 最后一个分区用 └─，其余用 ├─
            if (( i == count - 1 )); then
                tree="└─"
            else
                tree="├─"
            fi

            if [[ -n "${real_size[$part]}" ]]; then
                size="${real_size[$part]}"
                fs="${real_fs[$part]}"
                mount="${real_mount[$part]}"

                # 只有当预设值与真实值不同时才覆盖
                if [[ -n "${file_system_choices[$part]}" && "${file_system_choices[$part]}" != "$fs" ]]; then
                    final_fs="${file_system_choices[$part]}"
                else
                    final_fs="$fs"
                fi
                if [[ -n "${mount_point_choices[$part]}" && "${mount_point_choices[$part]}" != "$mount" ]]; then
                    final_mount="${mount_point_choices[$part]}"
                else
                    final_mount="$mount"
                fi
            else
                size="-"
                final_fs="${file_system_choices[$part]:--}"
                final_mount="${mount_point_choices[$part]:--}"
            fi

            # 树符号宽度2，空格1，设备名宽度17，总宽20，与磁盘行设备名对齐
            printf "%-2s %-17s %10s %10s %s\n" "$tree" "$part" "$size" "$final_fs" "$final_mount"
        done
    done
}
