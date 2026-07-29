#!/usr/bin/env bash
set -euo pipefail

# 初始化
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SRC_DIR/setup-script-functions/permission-check.sh"
source "$SRC_DIR/setup-script-functions/confirm.sh"
source "$SRC_DIR/setup-script-functions/basic-setter.sh"
source "$SRC_DIR/setup-script-functions/extra-driver-installer.sh"
source "$SRC_DIR/setup-script-functions/critical-component-installer.sh"
source "$SRC_DIR/setup-script-functions/desktop-environment-installer.sh"
source "$SRC_DIR/setup-script-functions/bootloader-installer.sh"
source "$SRC_DIR/setup-script-functions/final-setter.sh"
source "$SRC_DIR/setup-script-functions/extra-software-installer.sh"

BLUETOOTH=""
DESKTOP_ENVIRONMENT=""
FIREWALL=""
PRINTER=""

# 权限检查
permission_check

# 基础系统配置
basic_setter

# 安装重要组件
critical_component_installer

# 安装额外驱动
extra_driver_installer

# 安装桌面环境
desktop_environment_installer

# 安装额外软件
extra_software_installer

# 安装引导
bootloader_installer

# 最终配置
final_setter

# 设置镜像站
if confirm "Do you want to use a mirror optimized for the China region?"; then
    echo 'Setting up software mirror site...'
    echo '################################################################################
############################ Arch Linux mirrorlist #############################
################################################################################

Server = https://mirrors.tuna.tsinghua.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.ustc.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.163.com/archlinux/$repo/os/$arch
Server = https://mirrors.bfsu.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.cqu.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.hit.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.hust.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.jcut.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.jlu.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.jxust.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.neusoft.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.nju.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.njupt.edu.cn/archlinux/$repo/os/$arch
Server = https://mirror.nyist.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.qlu.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.qvq.net.cn/archlinux/$repo/os/$arch
Server = https://mirror.redrock.team/archlinux/$repo/os/$arch
Server = https://mirrors.shanghaitech.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.sjtug.sjtu.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.wsyu.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.xjtu.edu.cn/archlinux/$repo/os/$arch' > "/etc/pacman.d/mirrorlist"
fi

echo "System configuration complete."
echo 'Please enter "exit" to manually exit the chroot environment.'
