#!/usr/bin/env bash
set -euo pipefail

# 初始化
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SRC_DIR/functions/processor/permission-check.sh"
source "$SRC_DIR/functions/processor/confirm.sh"
source "$SRC_DIR/functions/processor/disk-detector.sh"
source "$SRC_DIR/functions/processor/mount-detector.sh"
source "$SRC_DIR/functions/processor/print-partition-table.sh"
source "$SRC_DIR/functions/processor/disk-selector.sh"
source "$SRC_DIR/functions/processor/partition-selector.sh"
source "$SRC_DIR/functions/processor/get-swap-size.sh"
source "$SRC_DIR/functions/setter/mount-point-setter.sh"
source "$SRC_DIR/functions/setter/file-system-setter.sh"
source "$SRC_DIR/functions/actuator/use-local-mirror.sh"
source "$SRC_DIR/functions/actuator/disk-wiper.sh"
source "$SRC_DIR/functions/actuator/partition-formatter.sh"
source "$SRC_DIR/functions/actuator/mounter.sh"
source "$SRC_DIR/functions/actuator/automatic-partitioner.sh"
source "$SRC_DIR/functions/actuator/manual-partitioner.sh"

SELECTED_DISK=""
SELECTED_PARTITION=""
TEMP_CONF_ITEM=""
declare -A file_system_choices=()
declare -A mount_point_choices=()

# 权限检查
permission_check

# 是否使用移动硬盘镜像站
if confirm "Do you want to use local mirror?"; then
    use_local_mirror
else
    # 不使用本地镜像站则检查网络
    if ! ping -c 3 baidu.com >/dev/null 2>&1; then
        echo 'Need Network Connection...'
        exit 1
    fi
fi
