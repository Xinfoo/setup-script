# =============================================================================
# systemd-boot entries and Secure Boot handoff / systemd-boot 启动项与 Secure Boot 衔接
# =============================================================================

configure_bootloader() {
    # Register shim in the target package database and run its package hooks. / 在目标包数据库登记 shim 并执行软件包 hook。
    if [[ "$ENABLE_SECURE_BOOT" == true ]]; then
        # Refuse a missing or replaced chroot handoff package. / 拒绝缺失或被替换的 chroot 传递软件包。
        if [[ ! -f /root/.arch-install-shim-signed.pkg.tar.zst ||
              -L /root/.arch-install-shim-signed.pkg.tar.zst ]]; then
            printf 'ERROR: the staged shim-signed package is missing or unsafe.\n' >&2
            return 1
        fi
        # Install through Pacman so package metadata and hooks are retained. / 通过 Pacman 安装，以保留软件包元数据并执行 hook。
        pacman -U --needed --noconfirm /root/.arch-install-shim-signed.pkg.tar.zst
        rm -f -- /root/.arch-install-shim-signed.pkg.tar.zst
    fi
    # Install systemd-boot files without asking bootctl to edit EFI variables. / 安装 systemd-boot 文件，但不让 bootctl 修改 EFI 变量。
    bootctl --no-variables install
    if [[ "$ENABLE_SECURE_BOOT" == true ]]; then
        # Prepare both the architecture-specific and removable-media paths. / 准备架构专用路径与可移动介质回退路径。
        install -d /boot/EFI/BOOT /boot/EFI/ARCH
        {
            # fallback.efi expects a BOM-prefixed UTF-16LE CSV description. / fallback.efi 需要带 BOM 的 UTF-16LE CSV 描述文件。
            printf '\xff\xfe'
            printf 'SHIMX64.EFI,Linux Boot Manager,,Linux Boot Manager\r\n' | iconv -f UTF-8 -t UTF-16LE
        } > /boot/EFI/ARCH/BOOTX64.CSV
    fi
    # Physical CPU modes load microcode before the common initramfs. / 物理 CPU 模式会在通用 initramfs 前加载微码。
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
        # Virtual-machine mode omits the microcode initrd line. / 虚拟机模式省略微码 initrd 行。
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
    # Replace the global loader policy with the legacy known-good settings. / 使用旧版已验证的设置覆盖全局 loader 策略。
    cat > /boot/loader/loader.conf <<'LOADER'
default arch.conf
editor no
timeout 3
console-mode keep
LOADER
}
