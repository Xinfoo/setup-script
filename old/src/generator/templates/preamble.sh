#!/usr/bin/bash
# Generated Arch Linux installation script; review the plan before running it.
# 自动生成的 Arch Linux 安装脚本；运行前请检查安装计划。
set -Eeuo pipefail
# Use a deterministic command search path and conservative default permissions. / 使用确定的命令搜索路径和保守的默认权限。
PATH='/usr/bin'
export PATH
readonly PATH
umask 022

# Resolve assets relative to the generated installer itself. / 相对于生成的安装脚本定位配套材料。
readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly ASSET_DIR="$SCRIPT_DIR"
readonly TARGET_ROOT='/mnt'
# Allow callers to select a fresh log path without changing the generated script. / 允许调用者在不修改脚本的情况下指定新的日志路径。
LOG_FILE=${ARCH_INSTALL_LOG:-}

# =============================================================================
# Generated installation plan / 自动生成的安装计划
# =============================================================================
