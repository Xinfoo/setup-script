# =============================================================================
# Installation orchestration / 安装流程编排
# =============================================================================

main() {
    # Secure Boot trusts an external package, so obtain explicit consent before all preparation. / Secure Boot 信任外部软件包，因此在所有准备前获取明确授权。
    confirm_secure_boot_package_source
    # Allocate the private workspace before any optional snapshots or backups. / 在创建任何可选快照或备份前分配私有工作目录。
    WORK_DIR=$(/usr/bin/mktemp -d /tmp/arch-install.XXXXXX) ||
        die 'Cannot create the private installer work directory.'
    chmod 0700 "$WORK_DIR"
    phase 'Preflight checks'
    preflight
    # Present and confirm all package-source effects before destructive consent. / 在破坏性授权前展示并确认全部软件源副作用。
    print_plan
    confirm_package_preparation
    prepare_package_source
    snapshot_secure_boot_assets
    probe_kept_filesystems
    confirm_destructive_actions
    # The final disk-path confirmation is followed by an immediate live-state recheck. / 最终磁盘路径确认后立即进行实时状态复核。
    phase 'Rechecking storage immediately before writes'
    verify_storage_state
    # Apply storage operations in partition, format, mount, and swap order. / 按分区、格式化、挂载和 Swap 顺序执行存储操作。
    partition_disk
    wait_for_partitions
    format_partitions
    mount_filesystems
    # Bootstrap the target before generating its self-contained chroot script. / 生成自包含 chroot 脚本前先引导安装目标系统。
    install_base_system
    write_chroot_script
    phase 'Configuring the installed system'
    arch-chroot "$TARGET_ROOT" /bin/bash /root/.arch-install-chroot.sh
    # The loopback repository is no longer needed after all target packages install. / 目标软件包全部安装后不再需要回环仓库。
    if [[ "$USE_LOCAL_MIRROR" == true ]]; then
        phase 'Stopping the temporary local mirror server'
        stop_local_mirror_server
    fi
    # Private-key signing and optional NVRAM registration remain outside chroot. / 私钥签名和可选 NVRAM 注册保留在 chroot 外执行。
    sign_secure_boot_assets
    create_firmware_entry
    rm -f -- \
        "$TARGET_ROOT/root/.arch-install-chroot.sh" \
        "$TARGET_ROOT/root/.arch-install-shim-signed.pkg.tar.zst"
    # Flush target writes before declaring success to the EXIT cleanup path. / 向 EXIT 清理路径声明成功前刷新目标写入。
    sync
    INSTALL_SUCCEEDED=true
    phase 'Installation configured; cleaning up mounts'
    printf 'Review the final cleanup result and log before rebooting: %s\n' "$LOG_FILE"
}

# Start only after every helper has been defined. / 所有辅助函数定义完成后才启动安装。
main "$@"
