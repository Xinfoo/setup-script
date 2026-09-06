# =============================================================================
# Services, user account, and mirror configuration / 服务、用户账户与镜像配置
# =============================================================================

configure_system() {
    local user_shell

    # Create every configuration drop-in directory idempotently. / 以幂等方式创建全部配置 drop-in 目录。
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
    # Enabling units only creates target-system links; no service is started inside chroot. / 启用单元只会创建目标系统链接，不会在 chroot 内启动服务。
    # Enable common services first, then features selected by the plan. / 先启用公共服务，再启用计划选择的功能。
    systemctl enable NetworkManager.service systemd-timesyncd.service fstrim.timer
    [[ "$HAS_BLUETOOTH" != true ]] || systemctl enable bluetooth.service
    [[ "$IS_LAPTOP" != true ]] || systemctl enable tlp.service
    [[ "$ENABLE_FIREWALL" != true ]] || systemctl enable firewalld.service
    [[ "$ENABLE_PRINTER" != true ]] || systemctl enable cups.socket
    # Exactly one display manager corresponds to the selected desktop. / 所选桌面只对应一个显示管理器。
    case "$DESKTOP" in
        kde) systemctl enable sddm.service ;;
        gnome) systemctl enable gdm.service ;;
        hyprland) systemctl enable greetd.service ;;
    esac
    # Select only a usable login shell and fail if neither supported path works. / 只选择可用的登录 Shell；两个受支持路径都不可用时终止。
    if [[ -x /usr/bin/zsh ]]; then
        user_shell=/usr/bin/zsh
    elif [[ -x /bin/zsh ]]; then
        user_shell=/bin/zsh
    else
        printf 'ERROR: zsh is not executable at /usr/bin/zsh or /bin/zsh.\n' >&2
        return 1
    fi
    # Create the ordinary wheel user only after choosing a valid login shell. / 选择有效登录 Shell 后才创建普通 wheel 用户。
    useradd -m -G wheel -s "$user_shell" "$USERNAME"
    # Install and validate a deterministic wheel sudoers drop-in. / 安装并校验确定性的 wheel sudoers drop-in。
    install -d -m 0750 /etc/sudoers.d
    printf '%%wheel ALL=(ALL:ALL) ALL\n' > /etc/sudoers.d/10-wheel
    chmod 0440 /etc/sudoers.d/10-wheel
    # Validate the complete sudoers graph so errors in includes are caught as well. / 校验完整 sudoers 引用图，使 include 中的错误也能被发现。
    visudo -cf /etc/sudoers
    # Set the ordinary account password interactively with the shared retry limit. / 使用共用重试上限交互设置普通账户密码。
    printf '\nSet the password for %s.\n' "$USERNAME"
    set_account_password "$USERNAME"
}

# Replace the target mirror list only when requested. / 仅在计划要求时替换目标系统镜像列表。
configure_mirrors() {
    [[ "$USE_CHINA_MIRRORS" == true ]] || return 0
    # Persist the legacy ordered China mirror list in the installed system. / 在目标系统中持久写入旧版顺序的中国镜像列表。
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
# Package installation precedes the final initramfs rebuild and bootloader setup. / 软件包安装先于最终 initramfs 重建与引导器配置。
configure_base
install_core_packages
install_drivers
install_desktop
install_optional_software
# Rebuild every selected kernel preset after driver and desktop changes. / 驱动与桌面改动完成后重建全部所选内核 preset。
mkinitcpio -P
configure_bootloader
configure_system
configure_mirrors
printf '\nChroot configuration complete.\n'
ARCH_CHROOT_SCRIPT
    # Restrict the generated chroot script to root execution. / 将生成的 chroot 脚本限制为仅 root 可执行。
    chmod 0700 "$TARGET_ROOT/root/.arch-install-chroot.sh"
}
