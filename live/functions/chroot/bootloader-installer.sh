#!/usr/bin/env bash

# 引导安装器
bootloader_installer() {
    local kernel="$(cat "$SRC_DIR/info/kernel-img.txt")"
    local micro_code="$(cat "$SRC_DIR/info/micro-code-img.txt")"
    local initramfs="$(cat "$SRC_DIR/info/initramfs-img.txt")"
    local root_uuid="$(cat "$SRC_DIR/info/root-uuid.txt")"

    bootctl install

    if [[ -z "$micro_code" ]]; then
        echo "title Arch Linux
linux /$kernel
initrd /$initramfs
options root=UUID=$root_uuid rw loglevel=3" > "/boot/loader/entries/arch.conf"

        echo "title Arch Linux Fallback
linux /$kernel
initrd /$initramfs
options root=UUID=$root_uuid rw loglevel=3" > "/boot/loader/entries/arch-fallback.conf"
    else
        echo "title Arch Linux
linux /$kernel
initrd /$micro_code
initrd /$initramfs
options root=UUID=$root_uuid rw loglevel=3" > "/boot/loader/entries/arch.conf"

        echo "title Arch Linux Fallback
linux /$kernel
initrd /$micro_code
initrd /$initramfs
options root=UUID=$root_uuid rw loglevel=3" > "/boot/loader/entries/arch-fallback.conf"
    fi

    echo "default arch.conf
editor no
timeout 3
console-mode keep" > "/boot/loader/loader.conf"
}
