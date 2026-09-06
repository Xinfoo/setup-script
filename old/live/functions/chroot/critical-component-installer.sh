#!/usr/bin/env bash

# 重要组件安装器
critical_component_installer() {
    # 是否启用TLP
    local tlp="$(cat "/info/tlp.txt")"
    # zsh相关软件包列表
    local -a zsh_packages_list=(
        "zsh"
        "zsh-completions"
        "zsh-autosuggestions"
        "zsh-syntax-highlighting"
        "grml-zsh-config")

    # 网络相关软件包列表
    local -a network_packages_list=(
        "networkmanager"
        "iwd"
        "dhcpcd" 
        "dhclient")

    # UEFI相关的工具
    local -a uefi_tools=(
        "efivar"
        "efitools"
        "efibootmgr"
        "sbsigntools"
        "mokutil")

    # 安装zsh
    pacman -S --needed --noconfirm ${zsh_packages_list[@]}

    # 安装网络
    pacman -S --needed --noconfirm ${network_packages_list[@]}

    # 安装uefi工具
    pacman -S --needed --noconfirm ${uefi_tools[@]}

    # 安装TLP
    if [[ "$tlp" == "yes" ]]; then
        pacman -S --needed --noconfirm tlp
    fi
}
