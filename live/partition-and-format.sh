#!/usr/bin/env bash
set -euo pipefail

permission_check() {
    if [[ "$EUID" -ne "0" ]]; then
        echo "Please run this script with root."
        exit 1
    fi
}

install_depends() {
    pacman -Sy --needed --noconfirm gdisk
}

disk_selector() {
    local disk
    local PS3="Select a disk: "
    local -a disk_list

    for block_path in "/sys/block/"*; do
        if [[ ! -d "$block_path" ]]; then
            continue
        fi

        if [[ ! -f "$block_path/device" ]]; then
            continue
        fi

        if [[ -f "$block_path/removable" ]] && [[ "$(cat "$block_path/removable")" == "0" ]];then
            disk="/dev/$(basename "$block_path")"
            disk_list+=("$disk")
        fi
    done

    if [[ "${#disk_list[@]}" == "0" ]];then
        echo "No usable disks available!" >&2
        return 1
    fi

    select choice in "${disk_list[@]}"; do
        if [[ -n $choice ]]; then
            echo "$choice"
            return 0
        else
            echo "Invalid selection, please choose a number from the list." >&2
        fi
    done
    return 1
}

disk_selector
