# =============================================================================
# Services, user account, and mirror configuration / 服务、用户账户与镜像配置
# =============================================================================

configure_system() {
    install -d /etc/NetworkManager/conf.d /etc/systemd/timesyncd.conf.d \
        /etc/systemd/coredump.conf.d /etc/systemd/journald.conf.d
    cat > /etc/NetworkManager/conf.d/wifi_backend.conf <<'NETWORK'
[device]
wifi.backend=iwd
NETWORK
    cat > /etc/systemd/timesyncd.conf.d/custom.conf <<'TIME'
[Time]
NTP=cn.ntp.org.cn time.windows.com cn.pool.ntp.org time.cloudflare.com
TIME
    cat > /etc/systemd/coredump.conf.d/custom.conf <<'COREDUMP'
[Coredump]
Storage=none
ProcessSizeMax=0
COREDUMP
    cat > /etc/systemd/journald.conf.d/custom.conf <<'JOURNAL'
[Journal]
SystemMaxUse=500M
SystemMaxFileSize=50M
JOURNAL
    systemctl enable NetworkManager.service systemd-timesyncd.service fstrim.timer
    [[ "$HAS_BLUETOOTH" != true ]] || systemctl enable bluetooth.service
    [[ "$IS_LAPTOP" != true ]] || systemctl enable tlp.service
    [[ "$ENABLE_FIREWALL" != true ]] || systemctl enable firewalld.service
    [[ "$ENABLE_PRINTER" != true ]] || systemctl enable cups.socket
    case "$DESKTOP" in
        kde) systemctl enable sddm.service ;;
        gnome) systemctl enable gdm.service ;;
        hyprland) systemctl enable greetd.service ;;
    esac
    useradd -m -G wheel -s /bin/zsh "$USERNAME"
    install -d -m 0750 /etc/sudoers.d
    printf '%%wheel ALL=(ALL:ALL) ALL\n' > /etc/sudoers.d/10-wheel
    chmod 0440 /etc/sudoers.d/10-wheel
    visudo -cf /etc/sudoers
    printf '\nSet the password for %s.\n' "$USERNAME"
    set_account_password "$USERNAME"
}

# Replace the target mirror list only when requested. / 仅在计划要求时替换目标系统镜像列表。
configure_mirrors() {
    [[ "$USE_CHINA_MIRRORS" == true ]] || return 0
    cat > /etc/pacman.d/mirrorlist <<'MIRRORS'
################################################################################
############################ Arch Linux mirrorlist #############################
################################################################################

Server = https://mirrors.tuna.tsinghua.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.ustc.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.163.com/archlinux/$repo/os/$arch
Server = https://mirrors.bfsu.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.cqu.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.hit.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.hust.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.jcut.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.jlu.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.jxust.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.neusoft.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.nju.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.njupt.edu.cn/archlinux/$repo/os/$arch
Server = https://mirror.nyist.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.qlu.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.qvq.net.cn/archlinux/$repo/os/$arch
Server = https://mirror.redrock.team/archlinux/$repo/os/$arch
Server = https://mirrors.shanghaitech.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.sjtug.sjtu.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.wsyu.edu.cn/archlinux/$repo/os/$arch
Server = https://mirrors.xjtu.edu.cn/archlinux/$repo/os/$arch
MIRRORS
}

# Apply target-system configuration in dependency order. / 按依赖顺序应用目标系统配置。
configure_base
install_core_packages
install_drivers
install_desktop
install_optional_software
mkinitcpio -P
configure_bootloader
configure_system
configure_mirrors
printf '\nChroot configuration complete.\n'
ARCH_CHROOT_SCRIPT
    chmod 0700 "$TARGET_ROOT/root/.arch-install-chroot.sh"
}
