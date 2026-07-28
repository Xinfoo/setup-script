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
