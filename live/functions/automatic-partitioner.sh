#!/usr/bin/env bash

# 自动分区器
automatic_partitioner() {

    if [[ -z "${1:-}" ]]; then
        return 1
    fi

    local choice
    local swap_size_mib
    local PS3="Select a partitioning plan: "
    swap_size_mib="$(get_swap_size)" || return 1

    echo "1>| EFI (1GiB) | ROOT (AUTO) | SWAP (AUTO) |" >&2
    echo "2>| EFI (1GiB) | ROOT (100GiB) | HOME (AUTO) | SWAP (AUTO) |" >&2
    echo "3>| EFI (1GiB) | ROOT (AUTO) |" >&2
    select choice in plan1 plan2 plan3; do
        case $choice in
            plan1)
                sfdisk "$1" <<EOF
label: gpt
unit: MiB

size=1024,type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B
size=-$swap_size_mib,type=0FC63DAF-8483-4772-8E79-3D69D8477DE4
type=0657FD6D-A4AB-43C4-84E5-0933C84B4F4F
EOF
                ;;
            plan2)
                ;;
            plan3)
                ;;
            *)
                echo "Please enter a valid number." >&2
                ;;
        esac
    done
}
