#!/usr/bin/env bash

# 权限检查器
permission_check() {
    if [[ "$EUID" -ne "0" ]]; then
        echo "Please run this script as root."
        exit 1
    fi
}
