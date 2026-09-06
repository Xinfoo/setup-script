# =============================================================================
# Desktop environment and input method / 桌面环境与输入法
# =============================================================================

# Write the complete Fcitx environment file with Arch's standard header.
# 写入完整的 Fcitx 环境变量文件，并保留 Arch 的标准文件头。
configure_fcitx_environment() {
    # This is a complete file, not an append, so repeated runs remain stable. / 这是完整文件而非追加写入，因此重复运行结果稳定。
    cat > /etc/environment <<'ENVIRONMENT'
#
# This file is parsed by pam_env module
#
# Syntax: simple "KEY=VAL" pairs on separate lines
#

XMODIFIERS=@im=fcitx
SDL_IM_MODULE=fcitx
GLFW_IM_MODULE=ibus
ENVIRONMENT
}

install_desktop() {
    # Each branch installs only the selected desktop and its optional additions. / 每个分支只安装所选桌面及其可选附加组件。
    case "$DESKTOP" in
        kde)
            # KDE recommendations and Fcitx remain independently selectable. / KDE 推荐软件与 Fcitx 可独立选择。
            pacman_install "${PKG_KDE[@]}"
            if [[ "$DESKTOP_RECOMMENDED" == true ]]; then
                pacman_install "${PKG_KDE_RECOMMENDED[@]}"
            fi
            if [[ "$CHINESE_INPUT" == true ]]; then
                pacman_install "${PKG_FCITX[@]}"
                configure_fcitx_environment
            fi
            ;;
        gnome)
            # GNOME laptop integration, recommendations, and IBus are separate groups. / GNOME 笔记本集成、推荐软件和 IBus 使用独立软件包组。
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
            # Hyprland uses greetd and ReGreet as the generated login environment. / Hyprland 使用 greetd 与 ReGreet 作为生成的登录环境。
            pacman_install "${PKG_HYPRLAND[@]}"
            if [[ "$CHINESE_INPUT" == true ]]; then
                pacman_install "${PKG_FCITX[@]}"
                configure_fcitx_environment
            fi
            # Point greetd at a dedicated Hyprland configuration for the greeter. / 让 greetd 使用专供登录界面的 Hyprland 配置。
            install -d /etc/greetd
            cat > /etc/greetd/config.toml <<'GREETD'
[terminal]
vt = 1

[default_session]
command = "dbus-run-session start-hyprland -- -c /etc/greetd/hyprland.lua"
user = "greeter"
GREETD
            # Keep the greeter compositor configuration in the legacy readable layout. / 保持旧版易读格式的登录界面合成器配置。
            cat > /etc/greetd/hyprland.lua <<'HYPRLAND'
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
HYPRLAND
            # Append only the GTK section to the package-provided ReGreet file. / 仅向软件包提供的 ReGreet 文件追加 GTK 段。
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
