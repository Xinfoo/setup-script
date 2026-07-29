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
INPUT_METHOD=""
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

echo "System configuration complete."
echo "Please enter "exit" to manually exit the chroot environment."