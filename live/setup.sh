#!/usr/bin/env bash
set -euo pipefail
source "./functions.sh"
source "./use-local-mirror.sh"

# 权限检查
permission_check

# 是否使用移动硬盘镜像站
if confirm "Do you want to use local mirror?"; than
    use_local_miror
fi
