#!/usr/bin/bash
set -Eeuo pipefail
PATH='/usr/bin'
export PATH
readonly PATH
umask 022

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly ASSET_DIR="$SCRIPT_DIR"
readonly TARGET_ROOT='/mnt'
LOG_FILE=${ARCH_INSTALL_LOG:-}
