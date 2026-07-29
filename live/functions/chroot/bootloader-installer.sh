#!/usr/bin/env bash

# 引导安装器
bootloader_installer() {
    local ROOT_UUID="$(cat "$SRC_DIR/ROOT-UUID.txt")"
    bootctl install

    echo "title Arch Linux
linux /vmlinuz-linux-zen
initrd /intel-ucode.img
initrd /initramfs-linux-zen.img
options root=UUID=$ROOT_UUID rw loglevel=3" > "/boot/loader/entries/arch.conf"

    echo "title Arch Linux Fallback
linux /vmlinuz-linux-zen
initrd /intel-ucode.img
initrd /initramfs-linux-zen.img
options root=UUID=$ROOT_UUID rw loglevel=3" > "/boot/loader/entries/arch-fallback.conf"

    echo "default arch.conf
editor no
timeout 3
console-mode keep" > "/boot/loader/loader.conf"
}
