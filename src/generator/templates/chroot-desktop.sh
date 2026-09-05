# =============================================================================
# Desktop environment and input method / 桌面环境与输入法
# =============================================================================

install_desktop() {
    case "$DESKTOP" in
        kde)
            pacman_install "${PKG_KDE[@]}"
            if [[ "$DESKTOP_RECOMMENDED" == true ]]; then
                pacman_install "${PKG_KDE_RECOMMENDED[@]}"
            fi
            if [[ "$CHINESE_INPUT" == true ]]; then
                pacman_install "${PKG_FCITX[@]}"
                cat >> /etc/environment <<'FCITX'
XMODIFIERS=@im=fcitx
SDL_IM_MODULE=fcitx
GLFW_IM_MODULE=ibus
FCITX
            fi
            ;;
        gnome)
            pacman_install "${PKG_GNOME[@]}"
            [[ "$IS_LAPTOP" != true ]] || pacman_install "${PKG_GNOME_LAPTOP[@]}"
            if [[ "$DESKTOP_RECOMMENDED" == true ]]; then
                pacman_install "${PKG_GNOME_RECOMMENDED[@]}"
            fi
            if [[ "$CHINESE_INPUT" == true ]]; then
                pacman_install "${PKG_IBUS[@]}"
            fi
            ;;
        hyprland)
            pacman_install "${PKG_HYPRLAND[@]}"
            if [[ "$CHINESE_INPUT" == true ]]; then
                pacman_install "${PKG_FCITX[@]}"
                cat >> /etc/environment <<'FCITX'
XMODIFIERS=@im=fcitx
SDL_IM_MODULE=fcitx
GLFW_IM_MODULE=ibus
FCITX
            fi
            install -d /etc/greetd
            cat > /etc/greetd/config.toml <<'GREETD'
[terminal]
vt = 1

[default_session]
command = "dbus-run-session start-hyprland -- -c /etc/greetd/hyprland.lua"
user = "greeter"
GREETD
            cat > /etc/greetd/hyprland.lua <<'HYPRLAND'
hl.monitor({ output = "", mode = "highrr", position = "auto", scale = "auto" })
hl.on("hyprland.start", function()
    hl.exec_cmd("regreet; hyprctl dispatch 'hl.dsp.exit()'")
end)
hl.config({ misc = { disable_hyprland_logo = true, disable_splash_rendering = true, disable_hyprland_guiutils_check = true } })
HYPRLAND
            cat >> /etc/greetd/regreet.toml <<'REGREET'

[GTK]
theme_name = "Materia"
icon_theme_name = "Papirus"
font_name = "Noto Sans 12"
application_prefer_dark_theme = true
REGREET
            ;;
        none) ;;
    esac
