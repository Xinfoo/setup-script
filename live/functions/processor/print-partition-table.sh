#!/usr/bin/env bash

# 输出分区状态表
print_partition_table() {
    local detected_disks lsblk_output line
    local item name size fs mount type parent part disk suffix i
    local planned_fs planned_mount tree display_device disk_label
    local is_different
    local device_width=24
    local -A real_fs=()
    local -A real_mount=()
    local -A real_size=()
    local -A real_parent=()
    local -A planned_mounts=()
    local -a disk_dev_path_list=()
    local -a all_disks=()
    local -a real_parts=()
    local -a actual_parts=()
    local -a planned_parts=()

    # 获取可用磁盘列表
    detected_disks="$(disk_detector)" || return 1
    for item in "$detected_disks"; do
        disk_dev_path_list+=("/dev/$item")
    done

    # 一次读取磁盘及其分区的真实状态
    lsblk_output="$(lsblk --paths --pairs --output NAME,SIZE,FSTYPE,MOUNTPOINT,TYPE,PKNAME --noheadings "${disk_dev_path_list[@]}")" || return 1
    while IFS= read -r line; do
        [[ -z "$line" ]] && continue

        [[ $line =~ NAME=\"([^\"]*)\" ]] || continue
        name="${BASH_REMATCH[1]}"
        [[ $line =~ SIZE=\"([^\"]*)\" ]] && size="${BASH_REMATCH[1]}" || size=""
        [[ $line =~ FSTYPE=\"([^\"]*)\" ]] && fs="${BASH_REMATCH[1]}" || fs=""
        [[ $line =~ MOUNTPOINT=\"([^\"]*)\" ]] && mount="${BASH_REMATCH[1]}" || mount=""
        [[ $line =~ [[:space:]]TYPE=\"([^\"]*)\" ]] && type="${BASH_REMATCH[1]}" || type=""
        [[ $line =~ PKNAME=\"([^\"]*)\" ]] && parent="${BASH_REMATCH[1]}" || parent=""

        printf -v name '%b' "$name"
        printf -v mount '%b' "$mount"
        printf -v parent '%b' "$parent"

        real_size["$name"]="$size"
        real_fs["$name"]="$fs"
        real_mount["$name"]="$mount"
        real_parent["$name"]="$parent"

        if [[ "$type" == "disk" ]]; then
            all_disks+=("$name")
        elif [[ "$type" == "part" ]]; then
            real_parts+=("$name")
        fi
    done <<< "$lsblk_output"

    if [[ ${#all_disks[@]} -eq 0 ]]; then
        echo "No disks found." >&2
        return 1
    fi

    # 统一将逻辑挂载点转换为安装环境中的实际路径
    for part in "${!mount_point_choices[@]}"; do
        planned_mount="${mount_point_choices[$part]}"
        case "$planned_mount" in
            "") ;;
            "/") planned_mount="/mnt" ;;
            "/mnt"|"/mnt/"*) ;;
            *) planned_mount="/mnt$planned_mount" ;;
        esac
        planned_mounts["$part"]="$planned_mount"
    done

    # 根据最长设备名调整首列宽度
    for name in "${!real_size[@]}" "${!file_system_choices[@]}" "${!mount_point_choices[@]}"; do
        if (( ${#name} + 3 > device_width )); then
            device_width=$((${#name} + 3))
        fi
    done
    for disk in "${all_disks[@]}"; do
        if (( ${#disk} + 9 > device_width )); then
            device_width=$((${#disk} + 9))
        fi
    done

    printf "%-${device_width}s %10s %-18s %s\n" "DEVICE" "SIZE" "FILESYSTEM" "MOUNTPOINT"
    printf "%-${device_width}s %10s %-18s %s\n" "------" "----" "----------" "----------"

    for disk in "${all_disks[@]}"; do
        actual_parts=()
        planned_parts=()

        # 使用 lsblk 返回的父设备关系收集真实分区
        for part in "${real_parts[@]}"; do
            if [[ "${real_parent[$part]:-}" == "$disk" ]]; then
                actual_parts+=("$part")
            fi
        done

        # 收集两个数组为该磁盘定义的新分区
        for part in "${!file_system_choices[@]}" "${!mount_point_choices[@]}"; do
            if [[ "$part" == "$disk"* ]]; then
                suffix="${part#"$disk"}"
                if [[ "$suffix" =~ ^p?[0-9]+$ ]]; then
                    planned_parts+=("$part")
                fi
            fi
        done

        if [[ ${#actual_parts[@]} -gt 0 ]]; then
            mapfile -t actual_parts < <(printf '%s\n' "${actual_parts[@]}" | sort -uV)
        fi
        if [[ ${#planned_parts[@]} -gt 0 ]]; then
            mapfile -t planned_parts < <(printf '%s\n' "${planned_parts[@]}" | sort -uV)
        fi

        is_different=false
        if [[ ${#planned_parts[@]} -gt 0 ]]; then
            if [[ "${actual_parts[*]}" != "${planned_parts[*]}" ]]; then
                is_different=true
            fi

            # 分区集合相同时，继续比较数组中明确指定的状态
            if [[ "$is_different" == false ]]; then
                for part in "${planned_parts[@]}"; do
                    planned_fs="${file_system_choices[$part]:-}"
                    planned_mount="${planned_mounts[$part]:-}"

                    if [[ -n "$planned_fs" && "$planned_fs" != "${real_fs[$part]:-}" ]]; then
                        is_different=true
                        break
                    fi

                    if [[ -n "$planned_mount" && "$planned_mount" != "${real_mount[$part]:-}" ]]; then
                        is_different=true
                        break
                    fi
                done
            fi
        fi

        # 输出当前真实状态
        disk_label="$disk"
        [[ "$is_different" == true ]] && disk_label="$disk(present)"
        size="${real_size[$disk]:--}"
        fs="${real_fs[$disk]:--}"
        mount="${real_mount[$disk]:--}"
        printf "%-${device_width}s %10s %-18s %s\n" "$disk_label" "$size" "$fs" "$mount"

        for (( i=0; i<${#actual_parts[@]}; i++ )); do
            part="${actual_parts[$i]}"
            tree="├─"
            (( i == ${#actual_parts[@]} - 1 )) && tree="└─"
            display_device="$tree $part"
            size="${real_size[$part]:--}"
            fs="${real_fs[$part]:--}"
            mount="${real_mount[$part]:--}"
            printf "%-$((device_width + 4))s %10s %-18s %s\n" "$display_device" "$size" "$fs" "$mount"
        done

        # 定义状态不同时，单独输出数组中的新状态
        if [[ "$is_different" == true ]]; then
            disk_label="$disk(new)"
            size="${real_size[$disk]:--}"
            printf "%-${device_width}s %10s %-18s %s\n" "$disk_label" "$size" "-" "-"

            for (( i=0; i<${#planned_parts[@]}; i++ )); do
                part="${planned_parts[$i]}"
                tree="├─"
                (( i == ${#planned_parts[@]} - 1 )) && tree="└─"
                display_device="$tree $part"

                fs="${file_system_choices[$part]:-${real_fs[$part]:--}}"
                planned_mount="${planned_mounts[$part]:-}"
                if [[ -n "$planned_mount" ]]; then
                    mount="$planned_mount"
                else
                    mount="${real_mount[$part]:--}"
                fi

                printf "%-$((device_width + 4))s %10s %-18s %s\n" "$display_device" "-" "$fs" "$mount"
            done
        fi
    done
}
