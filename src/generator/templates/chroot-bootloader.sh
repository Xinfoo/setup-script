configure_bootloader() {
    bootctl --no-variables install
    if [[ "$ENABLE_SECURE_BOOT" == true ]]; then
        install -d /boot/EFI/BOOT /boot/EFI/ARCH
        {
            printf '\xff\xfe'
            printf 'SHIMX64.EFI,Arch Linux,,Arch Linux Secure Boot\r\n' | iconv -f UTF-8 -t UTF-16LE
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
initrd /$FALLBACK_FILE
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
initrd /$FALLBACK_FILE
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

