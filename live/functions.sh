#!/usr/bin/env bash

# 权限检查器
permission_check() {
    if [[ "$EUID" -ne "0" ]]; then
        echo "Please run this script with root."
        exit 1
    fi
}

# 处理确认操作
confirm() {
    local choice
    local tmp_choice

    while true; do
        read -rp "$1 [Y/n] " choice
        tmp_choice=$(echo "$choice" | tr '[:upper:]' '[:lower:]')

        if [[ "$tmp_choice" == "y" || -z "$tmp_choice" ]]; then
            return 0
        elif [[ "$tmp_choice" == "n" ]]; then
            return 1
        fi
    done
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

# 分区格式化器
partition_formatter() {
    # 从关联数组中获取所要格式化的文件系统
    local file_system="${file_system_choices["$1"]}"

    # 格式化文件系统
    case $file_system in
        "FAT32")
            mkfs.fat -F 32 "$1"
            ;;
        "Ext4")
            mkfs.ext4 -F "$1"
            ;;
        "XFS")
            mkfs.xfs -f "$1"
            ;;
        "F2FS")
            mkfs.f2fs -f -O extra_attr,inode_checksum,sb_checksum,compression "$1"
            ;;
        "SWAP")
            mkswap -f "$1"
            ;;
    esac
}

# 挂载点挂载器
mounter() {
    # 定义要使用的变量
    local mount_point="${mount_point_choices["$1"]}"
    local file_system="${file_system_choices["$1"]}"
    local PS3="Select mount options: "

    # 针对F2FS的挂载选项特殊处理
    if [[ "$file_system" == "F2FS" ]]; then
        echo "What mount options would you like to enable for your $mount_point F2FS?" >&2
        echo "1>Optimized mount options(recommended)" >&2
        echo "2>Default mount options" >&2
        select choice in Optimized Default; do
            if [[ "$mount_point" == "/" ]]; then
                case $choice in
                    Optimized)
                        mount -o noatime,lazytime,background_gc=off,atgc,nodiscard,fsync_mode=nobarrier "$1" "$mount_point"
                        return 0
                        ;;
                    Default)
                        mount -o "$1" "$mount_point"
                        return 0
                        ;;
                esac
            else
                case $choice in
                    Optimized)
                        mount --mkdir -o noatime,lazytime,background_gc=off,atgc,nodiscard,fsync_mode=nobarrier "$1" "$mount_point"
                        return 0
                        ;;
                    Default)
                        mount --mkdir "$1" "$mount_point"
                        return 0
                        ;;
                esac
            fi
        done
    fi

    # 挂载普通分区
    mount --mkdir "$1" "$mount_point"
}

# 文件系统选择器
file_system_selector() {
    # 定义要使用的变量和数组
    local PS3="Select the file system you want to format: "
    local -a file_system_list=("Ext4" "XFS" "F2FS")

    # 检测是否传递EFI参数
    if [[ "$1" == "--efi" ]]; then
        file_system_choices["$2"]="FAT32"
        return 0
    fi

    # 选择要格式化的分区并关联数组
    select choice in ${file_system_list[@]}; do
        case $choice in
            "Ext4")
                file_system_choices["$1"]="Ext4"
                ;;
            "XFS")
                file_system_choices["$1"]="XFS"
                ;;
            "F2FS")
                file_system_choices["$1"]="F2FS"
                ;;
            "SWAP")
                file_system_choices["$1"]="SWAP"
        esac
    done
}

# 挂载点选择器
mount_point_selector() {
    local PS3=""
    local -a mount_point_list=("/home" "/var" "/usr" "/opt")
    local -a temp_mount_point_list=()

    # 带有--root参数，直接挂载为根分区
    if [[ "$1" == "--root" ]]; then
        mount_point_choices["$2"]="/"
    fi

    # 带有--efi参数，直接挂载为/boot分区
    if [[ "$1" == "--efi" ]]; then
        mount_point_choices["$2"]="/boot"
    fi

    # 将已经使用过的挂载点移除
    for mounted_point in ${mount_point_choices[@]}; do
        if [[ -n $(echo "${mount_point_list[@]}" | grep "$mounted_point") ]]; then
            for mount_point in ${mount_point_list[@]}; do
                if [[ "$mount_point" != "$mounted_point" ]]; then
                    temp_mount_point_list+=("$mount_point")
                    mount_point_list=("${temp_mount_point_list[@]}")
                fi
            done
        fi
    done

    # 检查是否为空数组
    if [[ "${#mount_point_list[@]}" == "0" ]]; then
        echo "No mount point available!" >&2
        return 1
    fi

    if confirm "Do you want to select a mount point for this disk?"; then
        select choice in ${mount_point_list[@]}; do
            echo "You have chosen $choice" >&2
            if confirm "Are you sure you've made the right choice?"; then
                mount_point_choices["$1"]="$choice"
                break
            fi
        done
    fi
}
