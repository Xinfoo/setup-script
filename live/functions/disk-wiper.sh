#!/usr/bin/env bash

# 硬盘擦除器
disk_wiper() {
    # 检查输入是否为空
    if [[ -z "$1" ]]; then
        return 1
    fi

    # 清除分区表
    wipefs -a "$1"

    # 如果目标为支持丢弃的SSD，则整盘清零
    if [[ "$(lsblk --noheadings --nodeps --raw --output ROTA "$1")" == "0" ]] && [[ "$(lsblk --discard --noheadings --nodeps --raw --output DISC-GRAN "$1")" != "0B" ]]; then
        blkdiscard -f "$1"
    fi
}
