#!/usr/bin/env bash

find_partition_by_mountpoint() {
    local key
    local target="$1"

    for key in "${!mount_point_choices[@]}"; do
        if [[ "${mount_point_choices["$key"]}" == "$target" ]]; then
            echo "$key"
            return 0
        fi
    done
}
