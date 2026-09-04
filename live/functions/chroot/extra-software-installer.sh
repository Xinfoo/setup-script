#!/usr/bin/env bash

# 额外软件安装器
extra_software_installer() {

    # 解压缩相关工具
    local -a decompression_tools=(
        "unrar"
        "7zip"
        "zip"
        "unzip")

    # 终端工具
    local -a terminal_tools=(
        "git"
        "openssh"
        "htop"
        "nvtop"
        "tmux"
        "lynx"
        "wget"
        "aria2"
        "rsync"
        "usbutils"
        "cmus")

    # 附加可选工具
    local -a extra_tools=(
        "kitty"
        "neovim"
        "neovide"
        "lua51"
        "luarocks"
        "fd"
        "ripgrep"
        "wl-clipboard"
        "npm"
        "vim"
        "mpv")

    # 桌面环境常用组件
    local -a desktop_software=(
        "chromium"
        "thunderbird"
        "libreoffice-fresh"
        "gimp")

    echo
    if confirm "Do you want to install a firewall?"; then
        FIREWALL="yes"
        pacman -S --needed --noconfirm firewalld
    fi

    echo
    if confirm "Do you want to install the printer components?"; then
        PRINTER="yes"
        pacman -S --needed --noconfirm cups system-config-printer
    fi

    echo
    echo "${decompression_tools[@]}" >&2
    if confirm "Do you want to install this software?"; then
        pacman -S --needed --noconfirm ${decompression_tools[@]}
    fi

    echo
    echo "${terminal_tools[@]}" >&2
    if confirm "Do you want to install this software?"; then
        pacman -S --needed --noconfirm ${terminal_tools[@]}
    fi

    echo
    echo "${extra_tools[@]}" >&2
    if confirm "Do you want to install this software?"; then
        pacman -S --needed --noconfirm ${extra_tools[@]}
    fi

    echo
    echo "${desktop_software[@]}" >&2
    if confirm "Do you want to install this software?"; then
        pacman -S --needed --noconfirm ${desktop_software[@]}
    fi
}
