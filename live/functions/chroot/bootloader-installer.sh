#!/usr/bin/env bash

# 引导安装器
bootloader_installer() {
    local secure_boot=""
    local kernel="$(cat "$SRC_DIR/info/kernel-img.txt")"
    local micro_code="$(cat "$SRC_DIR/info/micro-code-img.txt")"
    local initramfs="$(cat "$SRC_DIR/info/initramfs-img.txt")"
    local root_uuid="$(cat "$SRC_DIR/info/root-uuid.txt")"

    echo "no" > "/info/secure-boot.txt"
    echo "no" > "/info/efi-variables.txt"

    # 检测安全启动工具和密钥是否存在，并问询安全启动
    if [[ -f "$SRC_DIR/shim-signed.pkg.tar.zst" ]] && [[ -d "/root/secure-boot/" ]]; then
        if confirm "Do you want to use Secure Boot?"; then
            pacman -U --needed --noconfirm "$SRC_DIR/shim-signed.pkg.tar.zst"
            secure_boot="yes"
            echo "yes" > "/info/secure-boot.txt"
        fi
    fi

    # 安装引导
    if [[ "$secure_boot" == "yes" ]]; then
        bootctl --no-variables install
        rm -f /boot/EFI/BOOT/*
        cp -a "/usr/share/shim-signed/shimx64.efi" "/boot/EFI/BOOT/BOOTX64.EFI"
        cp -a "/usr/share/shim-signed/mmx64.efi" "/boot/EFI/BOOT/MMX64.EFI"
        cp -a "/usr/share/shim-signed/fbx64.efi" "/boot/EFI/BOOT/FBX64.EFI"
        mkdir "/boot/EFI/ARCH"
        cp -a "/usr/share/shim-signed/shimx64.efi" "/boot/EFI/ARCH/SHIMX64.EFI"
        cp -a "/usr/share/shim-signed/mmx64.efi" "/boot/EFI/ARCH/MMX64.EFI"
        cp -a "/usr/share/shim-signed/fbx64.efi" "/boot/EFI/ARCH/FBX64.EFI"
        mv "/boot/$kernel" "/boot/$kernel.bak"
        sbsign --key "/root/secure-boot/MOK.key" --cert "/root/secure-boot/MOK.crt" --output "/boot/EFI/BOOT/GRUBX64.EFI" "/boot/EFI/systemd/systemd-bootx64.efi"
        sbsign --key "/root/secure-boot/MOK.key" --cert "/root/secure-boot/MOK.crt" --output "/boot/EFI/ARCH/GRUBX64.EFI" "/boot/EFI/systemd/systemd-bootx64.efi"
        sbsign --key "/root/secure-boot/MOK.key" --cert "/root/secure-boot/MOK.crt" --output "/boot/$kernel" "/boot/$kernel.bak"
    else
        bootctl --no-variables install
    fi

    # 是否创建启动项
    if confirm "Create a boot entry for the boot manager?"; then
        echo "yes" > "/info/efi-variables.txt"
    fi

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
