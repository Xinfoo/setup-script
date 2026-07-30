#!/usr/bin/env bash

# 桌面环境安装器
desktop_environment_installer() {
    local choice
    local PS3="Select a desktop environment: "

    # 桌面环境列表
    local -a desktop_environment=(
        "KDE Plasma"
        "Gnome")

    # kde桌面额外软件
    local -a kde_extra_packages=(
        "konsole"
        "dolphin"
        "ark"
        "kate"
        "partitionmanager"
        "filelight"
        "kcalc"
        "gwenview"
        "okular"
        "kcharselect"
        "ksystemlog"
        "kompare"
        "kid3"
        "haruna")
    # kde输入法
    local -a kde_input_method=(
        "fcitx5-im"
        "fcitx5-chinese-addons")

    # gnome桌面额外软件
    local -a gnome_extra_packages=(
        "dconf-editor"
        "gnome-tweaks"
        "gnome-shell-extension-appindicator")
    
    # gnome输入法
    local -a gnome_input_method=(
        "ibus"
        "ibus-libpinyin")

    # 字体包
    local -a fonts=(
        "noto-fonts"
        "noto-fonts-cjk"
        "noto-fonts-emoji"
        "noto-fonts-extra"
        "ttf-jetbrains-mono"
        "ttf-dejavu"
        "ttf-nerd-fonts-symbols"
        "ttf-nerd-fonts-symbols-mono")

    # 选择桌面环境安装
    echo "Select a desktop environment you want to install." >&2
    select choice in "${desktop_environment[@]}"; do
        case "$REPLY" in
            1)
                DESKTOP_ENVIRONMENT="KDE"
                pacman -S --needed plasma sddm-kcm

                if confirm "Do you want to install the recommended software for KDE Plasma?"; then
                    pacman -S --needed --noconfirm ${kde_extra_packages[@]}
                fi

                if confirm "Do you want to install a Chinese input method?"; then
                    pacman -S --needed --noconfirm ${kde_input_method[@]}
                fi

                echo '
XMODIFIERS=@im=fcitx
SDL_IM_MODULE=fcitx
GLFW_IM_MODULE=ibus' >> "/etc/environment"

                break
                ;;
            2)
                DESKTOP_ENVIRONMENT="Gnome"
                pacman -S --needed gnome gdm

                if confirm "Do you want to install the recommended GNOME software?"; then
                    pacman -S --needed --noconfirm ${gnome_extra_packages[@]}
                fi

                if confirm "Do you want to install a Chinese input method?"; then
                    pacman -S --needed --noconfirm ${gnome_input_method[@]}
                fi

                break
                ;;
            *)
                echo "Invalid selection, please choose a number from the list." >&2
                ;;
        esac
    done

    # 安装字体
    pacman -S --needed --noconfirm ${fonts[@]}
}
