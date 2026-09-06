#!/usr/bin/env bash

# 计算swap大小
get_swap_size() {
    local mem_mib
    local swap_mib

    mem_mib=$(awk '/MemTotal/ {print int($2/1024)}' /proc/meminfo)

    echo "Detected RAM: ${mem_mib} MiB" >&2

    if (( mem_mib <= 2048 )); then
        swap_mib=$((mem_mib * 2))

    elif (( mem_mib <= 8192 )); then
        swap_mib=$((mem_mib * 2))

    elif (( mem_mib <= 65536 )); then
        swap_mib=$mem_mib

    else
        swap_mib=8192
    fi

    echo "${swap_mib}"
}
