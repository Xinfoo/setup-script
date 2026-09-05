ROOT_UUID=$(blkid -s UUID -o value -- "$ROOT_DEVICE") || {
    printf 'Cannot determine the root filesystem UUID.\n' >&2
    exit 1
}
[[ -n "$ROOT_UUID" ]] || { printf 'The root filesystem UUID is empty.\n' >&2; exit 1; }
readonly ROOT_UUID
readonly KERNEL_FILE="vmlinuz-$KERNEL_IMAGE"
readonly INITRAMFS_FILE="initramfs-$KERNEL_IMAGE.img"
readonly FALLBACK_FILE="initramfs-$KERNEL_IMAGE-fallback.img"
readonly MICROCODE_FILE="${MICROCODE_PACKAGE:+$MICROCODE_PACKAGE.img}"

pacman_install() {
    (( $# > 0 )) || return 0
    pacman -S --needed --noconfirm "$@"
}

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

install_core_packages() {
    pacman_install "${PKG_CORE[@]}"
    [[ "$IS_LAPTOP" != true ]] || pacman_install "${PKG_LAPTOP_TOOLS[@]}"
}

install_drivers() {
    if [[ "$INTEL_GRAPHICS" == true ]]; then
        pacman_install "${PKG_INTEL_GRAPHICS[@]}"
    fi
    if [[ "$NVIDIA_GRAPHICS" == true ]]; then
        sed -i -E 's/^MODULES=.*/MODULES=(nvidia nvidia_modeset nvidia_uvm nvidia_drm)/' /etc/mkinitcpio.conf
        sed -i -E '/^HOOKS=/s/(^|[ (])kms([ )]|$)/\1\2/' /etc/mkinitcpio.conf
        pacman_install "${PKG_NVIDIA_GRAPHICS[@]}"
    fi
    if [[ "$HAS_BLUETOOTH" == true ]]; then
        pacman_install "${PKG_BLUETOOTH[@]}"
    fi
}

