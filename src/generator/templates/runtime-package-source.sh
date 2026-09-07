# =============================================================================
# Package source preparation and base installation / 软件包源准备与基础系统安装
# =============================================================================

prepare_package_source() {
    # Select exactly one source architecture for all following package operations. / 为后续全部软件包操作选择唯一的软件源架构。
    if [[ "$USE_LOCAL_MIRROR" == true ]]; then
        setup_local_mirror
    else
        # Preserve the Live configuration so EXIT cleanup can undo the temporary mirror selection. / 保存 Live 配置，使 EXIT 清理能够撤销临时镜像选择。
        cp -a /etc/pacman.conf "$WORK_DIR/host-pacman.conf"
        cp -a /etc/pacman.d/mirrorlist "$WORK_DIR/host-mirrorlist"
        HOST_PACMAN_CHANGED=true
        # Rate HTTPS mirrors in mainland China and stage the result before replacing mirrorlist. / 对中国大陆 HTTPS 镜像测速排序，并在替换 mirrorlist 前暂存结果。
        phase 'Ranking network mirrors in China'
        reflector --country China --protocol https --sort rate \
            --save "$WORK_DIR/network-mirrorlist"
        [[ -s "$WORK_DIR/network-mirrorlist" ]] ||
            die 'Reflector returned an empty China mirror list.'
        install -m 0644 -- "$WORK_DIR/network-mirrorlist" /etc/pacman.d/mirrorlist
        # Network repositories refresh immediately without an extra acknowledgement. / 网络仓库无需额外确认，直接刷新数据库。
        phase 'Refreshing package databases from ranked mirrors'
        pacman -Syy --noconfirm
    fi
    if [[ "$ENABLE_SECURE_BOOT" == true ]]; then
        # Signing tools are needed in the Live environment before disk writes begin. / 磁盘写入开始前，Live 环境必须具备签名工具。
        phase 'Installing the Live signing tool'
        (( ${#LIVE_SIGNING_PACKAGES[@]} > 0 )) ||
            die 'The Secure Boot Live package group is empty.'
        pacman -S --needed --noconfirm "${LIVE_SIGNING_PACKAGES[@]}"
        # Package installation is not enough: verify every command consumed by later templates. / 仅安装软件包还不够；继续确认后续模板使用的每条命令。
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
        # Active mounts and swap are the sole source of truth for the final table. / 活动挂载与 Swap 是最终表格的唯一事实来源。
        genfstab -U "$TARGET_ROOT"
    } > "$TARGET_ROOT/etc/fstab"
}
