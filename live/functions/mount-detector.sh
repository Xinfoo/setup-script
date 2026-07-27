#!/usr/bin/env bash

# 挂载检查器
mount_detector() {
    if [[ -z "${1:-}" ]]; then
        return 1
    fi

    if findmnt --source "$1" >& /dev/null; then
        echo "This partition has already been mounted." >&2
        return 1
    else
        return 0
    fi
}
