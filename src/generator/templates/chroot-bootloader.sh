# =============================================================================
# systemd-boot entries and Secure Boot handoff / systemd-boot 启动项与 Secure Boot 衔接
# =============================================================================

configure_bootloader() {
    # Register shim in the target package database and run its package hooks. / 在目标包数据库登记 shim 并执行软件包 hook。
    if [[ "$ENABLE_SECURE_BOOT" == true ]]; then
        if [[ ! -f /root/.arch-install-shim-signed.pkg.tar.zst ||
              -L /root/.arch-install-shim-signed.pkg.tar.zst ]]; then
            printf 'ERROR: the staged shim-signed package is missing or unsafe.\n' >&2
            return 1
        fi
        pacman -U --needed --noconfirm /root/.arch-install-shim-signed.pkg.tar.zst
        rm -f -- /root/.arch-install-shim-signed.pkg.tar.zst
    fi
    bootctl --no-variables install
    if [[ "$ENABLE_SECURE_BOOT" == true ]]; then
        install -d /boot/EFI/BOOT /boot/EFI/ARCH
        {
            printf '\xff\xfe'
            printf 'SHIMX64.EFI,Linux Boot Manager,,Linux Boot Manager\r\n' | iconv -f UTF-8 -t UTF-16LE
        } > /boot/EFI/ARCH/BOOTX64.CSV
    fi
    if [[ -n "$MICROCODE_FILE" ]]; then
        cat > /boot/loader/entries/arch.conf <<ENTRY
title Arch Linux
linux /$KERNEL_FILE
initrd /$MICROCODE_FILE
initrd /$INITRAMFS_FILE
options root=UUID=$ROOT_UUID rw loglevel=3
ENTRY
        cat > /boot/loader/entries/arch-fallback.conf <<ENTRY
title Arch Linux Fallback
linux /$KERNEL_FILE
initrd /$MICROCODE_FILE
initrd /$INITRAMFS_FILE
options root=UUID=$ROOT_UUID rw loglevel=3
ENTRY
    else
        cat > /boot/loader/entries/arch.conf <<ENTRY
title Arch Linux
linux /$KERNEL_FILE
initrd /$INITRAMFS_FILE
options root=UUID=$ROOT_UUID rw loglevel=3
ENTRY
        cat > /boot/loader/entries/arch-fallback.conf <<ENTRY
title Arch Linux Fallback
linux /$KERNEL_FILE
initrd /$INITRAMFS_FILE
options root=UUID=$ROOT_UUID rw loglevel=3
ENTRY
    fi
    cat > /boot/loader/loader.conf <<'LOADER'
default arch.conf
editor no
timeout 3
console-mode keep
LOADER
}
