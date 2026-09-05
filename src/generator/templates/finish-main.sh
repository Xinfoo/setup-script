# =============================================================================
# Installation orchestration / 安装流程编排
# =============================================================================

main() {
    WORK_DIR=$(/usr/bin/mktemp -d /tmp/arch-install.XXXXXX) ||
        die 'Cannot create the private installer work directory.'
    chmod 0700 "$WORK_DIR"
    phase 'Preflight checks'
    preflight
    print_plan
    confirm_package_preparation
    prepare_package_source
    snapshot_secure_boot_assets
    probe_kept_filesystems
    confirm_destructive_actions
    phase 'Rechecking storage immediately before writes'
    verify_storage_state
    partition_disk
    wait_for_partitions
    format_partitions
    mount_filesystems
    install_base_system
    write_chroot_script
    phase 'Configuring the installed system'
    arch-chroot "$TARGET_ROOT" /bin/bash /root/.arch-install-chroot.sh
    unmount_target_local_mirror
    sign_secure_boot_assets
    finalize_target_package_config
    create_firmware_entry
    rm -f -- "$TARGET_ROOT/root/.arch-install-chroot.sh"
    sync
    INSTALL_SUCCEEDED=true
    phase 'Installation configured; cleaning up mounts'
    printf 'Review the final cleanup result and log before rebooting: %s\n' "$LOG_FILE"
}

# Start only after every helper has been defined. / 所有辅助函数定义完成后才启动安装。
main "$@"
