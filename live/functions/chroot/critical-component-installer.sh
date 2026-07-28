#!/usr/bin/env bash

# 重要组件安装器
critical_component_installer() {
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
        "iftop"
        "nethogs")

    # 安装zsh
    pacman -S --needed --noconfirm ${zsh_packages_list[@]}

    # 安装网络
    pacman -S --needed --noconfirm ${network_packages_list[@]}
}
