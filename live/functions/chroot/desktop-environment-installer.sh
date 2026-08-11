#!/usr/bin/env bash

# 桌面环境安装器
desktop_environment_installer() {
    local choice
    local tlp="$(cat "/info/tlp.txt")"
    local PS3="Select a desktop environment: "

    # 桌面环境列表
    local -a desktop_environment=(
        "KDE Plasma"
        "Gnome"
        "hyprland(Experimental)"
        "Do not install a desktop environment")

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
        "file-roller"
        "gnome-shell-extension-appindicator")

    # gnome输入法
    local -a gnome_input_method=(
        "ibus"
        "ibus-libpinyin")

    # hyprland组件
    local -a hyprland_packages=(
        "uwsm"
        "greetd"
        "greetd-regreet"

        "hyprland"
        "hyprpolkitagent"
        "hyprpaper"
        "hyprpicker"
        "hyprshutdown"

        "waybar"
        "cliphist"
        "wofi"
        "playerctl"
        "brightnessctl"
        "libnotify"
        "pavucontrol"
        "network-manager-applet"
        "blueman"
        "mako"

        "pipewire"
        "pipewire-jack"
        "pipewire-alsa"
        "pipewire-pulse"
        "wireplumber"

        "xdg-desktop-portal"
        "xdg-desktop-portal-hyprland"
        "xdg-desktop-portal-gtk"
        "xdg-user-dirs"

        "wl-clipboard"
        "grim"
        "slurp"
        "swayimg"

        "kvantum"
        "kvantum-qt5"
        "nwg-look"
        "qt5-wayland"
        "qt6-wayland"
        "qt5ct"
        "qt6ct"

        "thunar"
        "gvfs"
        "gvfs-smb"
        "gvfs-mtp"
        "tumbler"
        "ffmpegthumbnailer"
        "file-roller"
        "thunar-archive-plugin"
        "thunar-media-tags-plugin"

        "papirus-icon-theme"
        "materia-gtk-theme"
        "kvantum-theme-materia")

    # 字体包
    local -a fonts=(
        "noto-fonts"
        "noto-fonts-cjk"
        "noto-fonts-emoji"
        "noto-fonts-extra"
        "ttf-sarasa-gothic"
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
                    echo '
XMODIFIERS=@im=fcitx
SDL_IM_MODULE=fcitx
GLFW_IM_MODULE=ibus' >> "/etc/environment"
                fi

                break
                ;;
            2)
                DESKTOP_ENVIRONMENT="Gnome"
                pacman -S --needed gnome gdm

                if [[ "$tlp" == "yes" ]]; then
                    pacman -S --needed --noconfirm tlp-pd
                fi

                if confirm "Do you want to install the recommended GNOME software?"; then
                    pacman -S --needed --noconfirm ${gnome_extra_packages[@]}
                fi

                if confirm "Do you want to install a Chinese input method?"; then
                    pacman -S --needed --noconfirm ${gnome_input_method[@]}
                fi

                break
                ;;
            3)
                DESKTOP_ENVIRONMENT="Hyprland"
                pacman -S --needed ${hyprland_packages[@]}

                if confirm "Do you want to install a Chinese input method?"; then
                    pacman -S --needed --noconfirm ${kde_input_method[@]}
                    echo '
XMODIFIERS=@im=fcitx
SDL_IM_MODULE=fcitx
GLFW_IM_MODULE=ibus' >> "/etc/environment"
                fi

                cat > "/etc/greetd/config.toml" << EOF
[terminal]
vt = 1

[default_session]
command = "dbus-run-session start-hyprland -- -c /etc/greetd/hyprland.lua"
user = "greeter"
EOF

                cat > "/etc/greetd/hyprland.lua" << EOF
hl.monitor({
    output   = "",
    mode     = "highrr",
    position = "auto",
    scale    = "auto",
})

hl.on("hyprland.start", function()
	hl.exec_cmd("regreet; hyprctl dispatch 'hl.dsp.exit()'")
end)
hl.config({
    misc = {
        disable_hyprland_logo = true,
        disable_splash_rendering = true,
        disable_hyprland_guiutils_check = true,
    },
})
EOF

                cat >> "/etc/greetd/regreet.toml" << EOF

[GTK]
theme_name = "Materia"
icon_theme_name = "Papirus"
font_name = "Noto Sans 12"
application_prefer_dark_theme = true
EOF

                break
                ;;
            4)
                break
                ;;
            *)
                echo "Invalid selection, please choose a number from the list." >&2
                ;;
        esac
    done

    # 安装字体
    pacman -S --needed --noconfirm ${fonts[@]}

    # 设置字体
    echo '<fontconfig>
    <alias>
        <family>sans-serif</family>
        <prefer>
            <family>Noto Sans</family>
            <family>Noto Sans CJK SC</family>
            <family>Noto Sans CJK TC</family>
            <family>Noto Sans CJK JP</family>
            <family>Noto Sans CJK KR</family>
            <family>Noto Color Emoji</family>
            <family>Symbols Nerd Font</family>
            <family>DejaVu Sans</family>
        </prefer>
    </alias>

    <alias>
        <family>serif</family>
        <prefer>
            <family>Noto Serif</family>
            <family>Noto Serif CJK SC</family>
            <family>Noto Serif CJK TC</family>
            <family>Noto Serif CJK JP</family>
            <family>Noto Serif CJK KR</family>
            <family>Noto Color Emoji</family>
            <family>Symbols Nerd Font</family>
            <family>DejaVu Serif</family>
        </prefer>
    </alias>

    <alias>
        <family>monospace</family>
        <prefer>
            <family>JetBrains Mono</family>
            <family>Sarasa Mono SC</family>
            <family>Sarasa Mono TC</family>
            <family>Sarasa Mono J</family>
            <family>Sarasa Mono K</family>
            <family>Noto Sans Mono</family>
            <family>Noto Color Emoji</family>
            <family>Symbols Nerd Font Mono</family>
            <family>DejaVu Sans Mono</family>
        </prefer>
    </alias>
</fontconfig>' > "/etc/fonts/local.conf"
}
