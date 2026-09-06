# =============================================================================
# Base system identity, packages, and drivers / 基础系统身份、软件包与驱动
# =============================================================================

ROOT_UUID=$(blkid -s UUID -o value -- "$ROOT_DEVICE") || {
    printf 'Cannot determine the root filesystem UUID.\n' >&2
    exit 1
}
[[ -n "$ROOT_UUID" ]] || { printf 'The root filesystem UUID is empty.\n' >&2; exit 1; }
readonly ROOT_UUID
readonly KERNEL_FILE="vmlinuz-$KERNEL_IMAGE"
readonly INITRAMFS_FILE="initramfs-$KERNEL_IMAGE.img"
readonly MICROCODE_FILE="${MICROCODE_PACKAGE:+$MICROCODE_PACKAGE.img}"

# Install a package list only when it is non-empty. / 仅在软件包列表非空时执行安装。
pacman_install() {
    (( $# > 0 )) || return 0
    pacman -S --needed --noconfirm "$@"
}

# Limit interactive password retries so installation cannot loop forever. / 限制交互式密码重试次数，避免安装无限循环。
set_account_password() {
    local account=$1 attempt
    for attempt in 1 2 3; do
        if passwd "$account"; then return 0; fi
        printf 'Password update failed for %s (attempt %d of 3).\n' \
            "$account" "$attempt" >&2
    done
    printf 'Giving up after three failed password attempts for %s.\n' "$account" >&2
    return 1
}

# Configure locale, clock, hostname, package database, and root password. / 配置区域、时钟、主机名、软件包数据库和 root 密码。
configure_base() {
    printf '\n==> Configuring locale, clock, and host identity\n'
    [[ -f "/usr/share/zoneinfo/$TIMEZONE" ]] || { printf 'Invalid timezone: %s\n' "$TIMEZONE" >&2; exit 1; }
    ln -sf -- "/usr/share/zoneinfo/$TIMEZONE" /etc/localtime
    hwclock --systohc
    sed -i 's/^#en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/;s/^#zh_CN.UTF-8 UTF-8/zh_CN.UTF-8 UTF-8/' /etc/locale.gen
    locale-gen
    printf 'LANG=%s\n' "$LOCALE" > /etc/locale.conf
    printf '%s\n' "$HOSTNAME_VALUE" > /etc/hostname
    cat > /etc/hosts <<HOSTS
# Static table lookup for hostnames.
# See hosts(5) for details.

127.0.0.1 localhost
::1 localhost
127.0.1.1 $HOSTNAME_VALUE.localdomain $HOSTNAME_VALUE
HOSTS
    printf 'KEYMAP=us\n' > /etc/vconsole.conf
    sed -i 's/^#Color$/Color/' /etc/pacman.conf
    pacman -Syy --noconfirm
    printf '\nSet the root password.\n'
    set_account_password root
}

# Install always-required packages and laptop additions. / 安装必需软件包及笔记本附加组件。
install_core_packages() {
    pacman_install "${PKG_CORE[@]}"
    [[ "$IS_LAPTOP" != true ]] || pacman_install "${PKG_LAPTOP_TOOLS[@]}"
}

# Install only the hardware support selected in the plan. / 仅安装计划中选择的硬件支持。
install_drivers() {
    if [[ "$INTEL_GRAPHICS" == true ]]; then
        pacman_install "${PKG_INTEL_GRAPHICS[@]}"
    fi
    if [[ "$NVIDIA_GRAPHICS" == true ]]; then
        sed -i -E 's/^MODULES=.*/MODULES=(nvidia nvidia_modeset nvidia_uvm nvidia_drm)/' /etc/mkinitcpio.conf
        # Remove kms without leaving doubled or edge whitespace. / 移除 kms，同时避免留下连续空格或括号边缘空格。
        sed -i -E '/^HOOKS=/ {
            s/[[:space:]]+kms[[:space:]]+/ /g
            s/\([[:space:]]*kms[[:space:]]+/(/g
            s/[[:space:]]+kms[[:space:]]*\)/)/g
            s/\([[:space:]]*kms[[:space:]]*\)/()/g
        }' /etc/mkinitcpio.conf
        pacman_install "${PKG_NVIDIA_GRAPHICS[@]}"
    fi
    if [[ "$HAS_BLUETOOTH" == true ]]; then
        pacman_install "${PKG_BLUETOOTH[@]}"
    fi
}
