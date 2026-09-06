    # Install shared fonts after the desktop-specific branch. / 桌面专用分支完成后安装共用字体。
    pacman_install "${PKG_FONTS[@]}"
    # Write the complete language-aware fallback chain shared by every desktop mode. / 写入所有桌面模式共用的完整语言感知字体回退链。
    cat > /etc/fonts/local.conf <<'FONTCONFIG'
<fontconfig>
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
</fontconfig>
FONTCONFIG
}

# Optional feature package groups / 可选功能软件包组
install_optional_software() {
    # Every flag maps directly to one editable packages.json group. / 每个标志直接对应 packages.json 中一个可编辑的软件包组。
    [[ "$ENABLE_FIREWALL" != true ]] || pacman_install "${PKG_FIREWALL[@]}"
    [[ "$ENABLE_PRINTER" != true ]] || pacman_install "${PKG_PRINTER[@]}"
    [[ "$INSTALL_ARCHIVE_TOOLS" != true ]] || pacman_install "${PKG_ARCHIVE_TOOLS[@]}"
    [[ "$INSTALL_TERMINAL_TOOLS" != true ]] || pacman_install "${PKG_TERMINAL_TOOLS[@]}"
    [[ "$INSTALL_EXTRA_TOOLS" != true ]] || pacman_install "${PKG_EXTRA_TOOLS[@]}"
    [[ "$INSTALL_DESKTOP_APPS" != true ]] || pacman_install "${PKG_DESKTOP_APPS[@]}"
}
