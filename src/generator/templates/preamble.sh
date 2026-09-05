#!/usr/bin/bash
# Generated Arch Linux installation script; review the plan before running it.
# 自动生成的 Arch Linux 安装脚本；运行前请检查安装计划。
set -Eeuo pipefail
PATH='/usr/bin'
export PATH
readonly PATH
umask 022

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly ASSET_DIR="$SCRIPT_DIR"
readonly TARGET_ROOT='/mnt'
LOG_FILE=${ARCH_INSTALL_LOG:-}

# =============================================================================
# Generated installation plan / 自动生成的安装计划
# =============================================================================
