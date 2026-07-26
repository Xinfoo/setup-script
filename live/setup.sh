#!/usr/bin/env bash
set -euo pipefail

# 初始化
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SRC_DIR/functions.sh"
source "$SRC_DIR/use-local-mirror.sh"
# source "./functions.sh"
# source "./use-local-mirror.sh"

SELECTED_DISK=""
TEMP_CONF_ITEM=""

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

# 是否擦除磁盘全新安装
while true; do
    if confirm "Do you want to erase some disks?"; then
        TEMP_CONF_ITEM="1"
        break
    else
        break
    fi
done
while true; do
    if [[ "$TEMP_CONF_ITEM" == "1" ]]; then
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
