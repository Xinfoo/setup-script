#!/usr/bin/env bash

# 额外驱动安装器
extra_driver_installer() {

    # NVIDIA驱动组件列表
    local -a nvidia_driver=(
        "nvidia-open-dkms"
        "nvidia-utils"
        "vdpauinfo")

    # 英伟达驱动问讯
    if confirm "Do you have an NVIDIA graphics card?"; then
        # Arch 的 mkinitcpio 默认 MODULES/HOOKS 行可能变化。
        # 这里是精确字符串替换，若默认文件格式变化，需要手动检查。
        sed -i 's/MODULES=()/MODULES=(nvidia nvidia_modeset nvidia_uvm nvidia_drm)/g' "/etc/mkinitcpio.conf"
        sed -i 's/HOOKS=(base systemd autodetect microcode modconf kms keyboard sd-vconsole block filesystems fsck)/HOOKS=(base systemd autodetect microcode modconf keyboard sd-vconsole block filesystems fsck)/g' "/etc/mkinitcpio.conf"
        pacman -S --needed --noconfirm ${nvidia_driver[@]}
    fi
}
