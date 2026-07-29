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
source "$SRC_DIR/functions/processor/find-partition-by-mountpoint.sh"
source "$SRC_DIR/functions/setter/mount-point-setter.sh"
source "$SRC_DIR/functions/setter/file-system-setter.sh"
source "$SRC_DIR/functions/actuator/use-local-mirror.sh"
source "$SRC_DIR/functions/actuator/disk-wiper.sh"
source "$SRC_DIR/functions/actuator/partition-formatter.sh"
source "$SRC_DIR/functions/actuator/mounter.sh"
source "$SRC_DIR/functions/actuator/automatic-partitioner.sh"
source "$SRC_DIR/functions/actuator/manual-partitioner.sh"
source "$SRC_DIR/functions/actuator/basic-software-installer.sh"

MAIN_INSTALL_DISK=""
SELECTED_DISK=""
PARTITION=""
PARTITION_ID=""
PS3="Enter a number: "
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

# 选择磁盘
clear
echo "Please select a primary disk on which to install the system."
MAIN_INSTALL_DISK="$(disk_selector)"

# 磁盘分区
options=(
    "Use the automatic partitioner and mounter"
    "Use the manually partitioner and mounter"
    )
echo "Please select a disk partitioning scheme."
select choice in "${options[@]}"; do
    case "$REPLY" in
        1)
            automatic_partitioner "$MAIN_INSTALL_DISK"
            break
            ;;
        2)
            manual_partitioner --main "$MAIN_INSTALL_DISK"
            while true; do
                if confirm "Do you want to partition other disks?"; then
                    SELECTED_DISK="$(disk_selector)"
                    manual_partitioner "$SELECTED_DISK"
                else
                    break 2
                fi
            done
            ;;
        *)
            echo "Invalid selection, please choose a number from the list."
            ;;
    esac
done

# 分区格式化
clear
print_partition_table
if confirm "The new disk partition will be formatted. Do you want to continue?"; then
    partition_formatter
else
    exit 1
fi

# 分区挂载
PARTITION="$(find_partition_by_mountpoint "/")"
if [[ -n "$PARTITION" ]]; then
    mounter "$PARTITION"
fi

PARTITION="$(find_partition_by_mountpoint "/boot")"
if [[ -n "$PARTITION" ]]; then
    mounter "$PARTITION"
fi

PARTITION="$(find_partition_by_mountpoint "/usr")"
if [[ -n "$PARTITION" ]]; then
    mounter "$PARTITION"
fi

PARTITION="$(find_partition_by_mountpoint "/var")"
if [[ -n "$PARTITION" ]]; then
    mounter "$PARTITION"
fi

PARTITION="$(find_partition_by_mountpoint "/home")"
if [[ -n "$PARTITION" ]]; then
    mounter "$PARTITION"
fi

PARTITION="$(find_partition_by_mountpoint "/opt")"
if [[ -n "$PARTITION" ]]; then
    mounter "$PARTITION"
fi

# 启用swap
for PARTITION in "${!file_system_choices[@]}"; do
    if [[ "${file_system_choices["$PARTITION"]}" == "swap" ]]; then
        mounter "$PARTITION"
    fi
done

echo "Disk partition mounting complete."
sleep 3

# 检查fstab生成器是否正确
clear
echo
genfstab -U /mnt
echo
if confirm "Is this generated fstab file correct?"; then
    echo "This file will ultimately be placed at /mnt/etc/fstab."
else
    exit 1
fi

# 安装基本软件
if confirm "The next step is to install the base system. Do you want to continue?"; then
    basic_software_installer
else
    exit 1
fi

# 生成fstab
genfstab -U /mnt >> /mnt/etc/fstab

# 获取根目录UUID
PARTITION="$(find_partition_by_mountpoint "/")"
echo "$(blkid -s UUID -o value "$PARTITION")" > "/mnt/ROOT-UUID.txt"

# 复制chroot内安装脚本
cp -ra "$SRC_DIR/functions/chroot/" "/mnt/setup-script-functions/"
cp -a "$SRC_DIR/functions/processor/confirm.sh" "/mnt/setup-script-functions/confirm.sh"
cp -a "$SRC_DIR/functions/processor/permission-check.sh" "/mnt/setup-script-functions/permission-check.sh"
cp -a "$SRC_DIR/chroot-setup.sh" "/mnt/setup.sh"

echo
echo "Please manually execute ./setup.sh"
arch-chroot -S /mnt

rm -rf "/mnt/setup-script-functions"
rm -f "/mnt/setup.sh"

# 卸载硬盘
echo "Unmounting partition..."
umount -R /mnt

# 生成EFI启动项
echo "Adding to startup entries..."
PARTITION="$(find_partition_by_mountpoint "/boot")"
PARTITION_ID="${PARTITION: -1}"
sleep 2
efibootmgr --create --disk $MAIN_INSTALL_DISK --part $PARTITION_ID --loader '\EFI\systemd\systemd-bootx64.efi' --label 'Linux Boot Manager' --unicode
echo "System installation complete!"
echo 'After that, type "reboot" to restart.'
