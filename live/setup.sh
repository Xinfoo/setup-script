#!/usr/bin/env bash
set -euo pipefail

# 初始化
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SRC_DIR/functions/permission-check.sh"
source "$SRC_DIR/functions/confirm.sh"
source "$SRC_DIR/functions/use-local-mirror.sh"
source "$SRC_DIR/functions/disk-detector.sh"
source "$SRC_DIR/functions/mount-detector.sh"
source "$SRC_DIR/functions/print-partition-table.sh"
source "$SRC_DIR/functions/disk-selector.sh"
source "$SRC_DIR/functions/partition-selector.sh"
source "$SRC_DIR/functions/mount-point-selector.sh"
source "$SRC_DIR/functions/file-system-selector.sh"
source "$SRC_DIR/functions/disk-wiper.sh"
source "$SRC_DIR/functions/partition-formatter.sh"
source "$SRC_DIR/functions/mounter.sh"
# source "./functions/permission-check.sh"
# source "./functions/confirm.sh"
# source "./functions/use-local-mirror.sh"
# source "./functions/disk-detector.sh"
# source "./functions/mount-detector.sh"
# source "./functions/print-partition-table.sh"
# source "./functions/disk-selector.sh"
# source "./functions/partition-selector.sh"
# source "./functions/mount-point-selector.sh"
# source "./functions/file-system-selector.sh"
# source "./functions/disk-wiper.sh"
# source "./functions/partition-formatter.sh"
# source "./functions/mounter.sh"

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

echo