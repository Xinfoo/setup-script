# =============================================================================
# Package source preparation and base installation / 软件包源准备与基础系统安装
# =============================================================================

prepare_package_source() {
    # Select exactly one source architecture for all following package operations. / 为后续全部软件包操作选择唯一的软件源架构。
    if [[ "$USE_LOCAL_MIRROR" == true ]]; then
        setup_local_mirror
    else
        phase 'Checking package repositories'
        pacman -Syy --noconfirm
    fi
    if [[ "$ENABLE_SECURE_BOOT" == true ]]; then
        # Signing tools are needed in the Live environment before disk writes begin. / 磁盘写入开始前，Live 环境必须具备签名工具。
        phase 'Installing the Live signing tool'
        (( ${#LIVE_SIGNING_PACKAGES[@]} > 0 )) ||
            die 'The Secure Boot Live package group is empty.'
        pacman -S --needed --noconfirm "${LIVE_SIGNING_PACKAGES[@]}"
        require_command sbsign
        require_command sbverify
        require_command bsdtar
    fi
    # Resolve the union early so missing optional packages fail before formatting. / 提前解析包集合并集，使可选包缺失在格式化前失败。
    phase 'Resolving the complete package selection'
    pacman -Sp --needed --noconfirm "${REQUIRED_PACKAGES[@]}" >/dev/null ||
        die 'One or more selected packages cannot be resolved before installation.'
}

# pacstrap installs the base system; genfstab records every active mount and swap. / pacstrap 安装基础系统；genfstab 记录全部活动挂载和交换空间。
install_base_system() {
    local packages=("${BOOTSTRAP_PACKAGES[@]}" "${KERNEL_PACKAGES[@]}" "${PLATFORM_PACKAGES[@]}")
    # Laptop firmware participates in pacstrap because it is needed from first boot. / 笔记本固件参与 pacstrap，确保首次启动即可使用。
    [[ "$IS_LAPTOP" != true ]] || packages+=("${LAPTOP_FIRMWARE_PACKAGES[@]}")
    (( ${#packages[@]} > 0 )) || die 'The bootstrap package selection is empty.'
    phase 'Installing the base system'
    pacstrap -K "$TARGET_ROOT" "${packages[@]}"
    # Replace fstab once with the standard header followed by genfstab output. / 使用标准文件头和 genfstab 输出一次性覆盖 fstab。
    {
        printf '%s\n' '# Static information about the filesystems.'
        printf '%s\n\n' '# See fstab(5) for details.'
        printf '%s\n' '# <file system> <dir> <type> <options> <dump> <pass>'
        genfstab -U "$TARGET_ROOT"
    } > "$TARGET_ROOT/etc/fstab"
}
