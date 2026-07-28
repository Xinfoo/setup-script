#!/usr/bin/env bash

# 自动分区器
automatic_partitioner() {

    if [[ -z "${1:-}" ]]; then
        return 1
    fi

    local choice
    local partition_tab
    local swap_size_mib
    local PS3="Select a partitioning plan: "

    local -a options=(
        "| EFI (1GiB) | ROOT (AUTO) | SWAP (AUTO) |"
        "| EFI (1GiB) | ROOT (100GiB) | HOME (AUTO) | SWAP (AUTO) |"
        "| EFI (1GiB) | ROOT (AUTO) |"
        )

    swap_size_mib="$(get_swap_size)" || return 1

    select choice in "${options[@]}"; do
        case "$REPLY" in
            1)
                sfdisk "$1" <<EOF
label: gpt
unit: MiB

size=1024,type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B
size=-$swap_size_mib,type=0FC63DAF-8483-4772-8E79-3D69D8477DE4
size=$swap_size_mib,type=0657FD6D-A4AB-43C4-84E5-0933C84B4F4F
EOF
                if [[ "$1" =~ ^"nvme" ]]; then
                    partition_tab="p"
                else
                    partition_tab=""
                fi

                # EFI
                file_system_setter --efi "${1}${partition_tab}1"
                mount_point_choices["${1}${partition_tab}1"]="/boot"

                # ROOT
                echo "Select a file system for formatting the ROOT partition." >&2
                file_system_setter --noswap "${1}${partition_tab}2"
                mount_point_choices["${1}${partition_tab}2"]="/"

                # SWAP
                file_system_setter --swap "${1}${partition_tab}3"

                return 0
                ;;
            2)
                sfdisk "$1" <<EOF
label: gpt
unit: MiB

size=1024,type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B
size=102400,type=0FC63DAF-8483-4772-8E79-3D69D8477DE4
size=-$swap_size_mib,type=0FC63DAF-8483-4772-8E79-3D69D8477DE4
size=$swap_size_mib,type=0657FD6D-A4AB-43C4-84E5-0933C84B4F4F
EOF
                if [[ "$1" =~ ^"nvme" ]]; then
                    partition_tab="p"
                else
                    partition_tab=""
                fi

                # EFI
                file_system_setter --efi "${1}${partition_tab}1"
                mount_point_choices["${1}${partition_tab}1"]="/boot"

                # ROOT
                echo "Select a file system for formatting the ROOT partition." >&2
                file_system_setter --noswap "${1}${partition_tab}2"
                mount_point_choices["${1}${partition_tab}2"]="/"

                # HOME
                echo "Select a file system for formatting the HOME partition." >&2
                file_system_setter --noswap "${1}${partition_tab}2"
                mount_point_choices["${1}${partition_tab}2"]="/home"

                # SWAP
                file_system_setter --swap "${1}${partition_tab}3"

                return 0
                ;;
            3)
                sfdisk "$1" <<EOF
lobel: gpt
unit: MiB

size=1024,type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B
size=,type=0FC63DAF-8483-4772-8E79-3D69D8477DE4
EOF
                if [[ "$1" =~ ^"nvme" ]]; then
                    partition_tab="p"
                else
                    partition_tab=""
                fi

                # EFI
                file_system_setter --efi "${1}${partition_tab}1"
                mount_point_choices["${1}${partition_tab}1"]="/boot"

                # ROOT
                echo "Select a file system for formatting the ROOT partition." >&2
                file_system_setter --noswap "${1}${partition_tab}2"
                mount_point_choices["${1}${partition_tab}2"]="/"

                return 0
                ;;
            *)
                echo "Please enter a valid number." >&2
                ;;
        esac
    done
}
