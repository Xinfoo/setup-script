#!/usr/bin/env bash
set -euo pipefail

# 初始化
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SRC_DIR/functions.sh"
source "$SRC_DIR/use-local-mirror.sh"
source "./functions.sh"
source "./use-local-mirror.sh"

SELECTED_DISK=""
SELECTED_PARTITION=""
TEMP_CONF_ITEM=""
declare -A file_system_choices=()
declare -A mount_point_choices=()

# 权限检查
permission_check

# 是否使用移动硬盘镜像站
if confirm "Do you want to use local mirror?"; then
    use_local_miror
else
    # 不使用本地镜像站则检查网络
    if ! ping -c 3 baidu.com >/dev/null 2>&1; then
        echo 'Need Network Connection...'
        exit 1
    fi
fi

echo

# 是否擦除磁盘全新安装
while true; do
    if confirm "Do you want to erase some disks?"; then
        TEMP_CONF_ITEM="yes"
        break
    else
        break
    fi
done

while true; do
    if [[ "$TEMP_CONF_ITEM" == "yes" ]]; then
        SELECTED_DISK="$(disk_selector)"
        if confirm "Are you sure you want to erase $SELECTED_DISK ? (Data connot be recovered)"; then
            disk_wiper "$SELECTED_DISK"
        fi
        if confirm "Do you want to erase the other disks?"; then
            continue
        fi
        break
    fi
done

echo

# 为硬盘分区
echo "1>Automatically partition and specify mount points (Will erase the disk)"
echo "2>Manually parition and manually specify mount points"
PS3="Select an option: "
select choice in Auto Manual ; do
    case $choice in
        Auto)
            TEMP_CONF_ITEM="auto"
            SELECTED_DISK="$(disk_selector)"
            if confirm "The selected disk will be erased. Do you want to continue?"; then
            fi
            ;;
        Manual)
            TEMP_CONF_ITEM="manual"
            break
            ;;
    esac
done

if [[ "$TEMP_CONF_ITEM" == "auto" ]]; then
    file_system_choices[""$SELECTED_DISK"p1"]="vfat"
    file_system_choices[""$SELECTED_DISK"p2"]="xfs"
    mount_point_choices[""$SELECTED_DISK"p1"]="/boot"
    mount_point_choices[""$SELECTED_DISK"p2"]="/"
elif [[ "$TEMP_CONF_ITEM" == "manual" ]]; then
else
    echo "ERROR!!" >&2
    exit 1
fi
