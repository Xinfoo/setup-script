# =============================================================================
# Final target package configuration / 目标系统软件包配置收尾
# =============================================================================

finalize_target_package_config() {
    [[ "$USE_LOCAL_MIRROR" == true ]] || return 0
    cp -a -- "$WORK_DIR/target-pacman.conf" "$TARGET_ROOT/etc/pacman.conf"
    if [[ "$USE_CHINA_MIRRORS" != true ]]; then
        cp -a -- "$WORK_DIR/target-mirrorlist" "$TARGET_ROOT/etc/pacman.d/mirrorlist"
    fi
    TARGET_CONFIG_FINALIZED=true
}
