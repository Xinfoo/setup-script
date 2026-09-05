    pacman_install "${PKG_FONTS[@]}"
    cat > /etc/fonts/local.conf <<'FONTCONFIG'
<fontconfig>
  <alias><family>sans-serif</family><prefer><family>Noto Sans</family><family>Noto Sans CJK SC</family><family>Noto Color Emoji</family><family>Symbols Nerd Font</family><family>DejaVu Sans</family></prefer></alias>
  <alias><family>serif</family><prefer><family>Noto Serif</family><family>Noto Serif CJK SC</family><family>Noto Color Emoji</family><family>Symbols Nerd Font</family><family>DejaVu Serif</family></prefer></alias>
  <alias><family>monospace</family><prefer><family>JetBrains Mono</family><family>Sarasa Mono SC</family><family>Noto Sans Mono</family><family>Noto Color Emoji</family><family>Symbols Nerd Font Mono</family><family>DejaVu Sans Mono</family></prefer></alias>
</fontconfig>
FONTCONFIG
}

install_optional_software() {
    [[ "$ENABLE_FIREWALL" != true ]] || pacman_install "${PKG_FIREWALL[@]}"
    [[ "$ENABLE_PRINTER" != true ]] || pacman_install "${PKG_PRINTER[@]}"
    [[ "$INSTALL_ARCHIVE_TOOLS" != true ]] || pacman_install "${PKG_ARCHIVE_TOOLS[@]}"
    [[ "$INSTALL_TERMINAL_TOOLS" != true ]] || pacman_install "${PKG_TERMINAL_TOOLS[@]}"
    [[ "$INSTALL_EXTRA_TOOLS" != true ]] || pacman_install "${PKG_EXTRA_TOOLS[@]}"
    [[ "$INSTALL_DESKTOP_APPS" != true ]] || pacman_install "${PKG_DESKTOP_APPS[@]}"
}

