# =============================================================================
# Chroot configuration script generation / Chroot 系统配置脚本生成
# =============================================================================

write_chroot_script() {
    # Stage the verified package for Pacman inside the target. / 将已验证的软件包暂存到目标系统供 Pacman 安装。
    if [[ "$ENABLE_SECURE_BOOT" == true ]]; then
        install -m 0600 -- \
            "$SECURE_BOOT_ASSET_SNAPSHOT/shim-signed.pkg.tar.zst" \
            "$TARGET_ROOT/root/.arch-install-shim-signed.pkg.tar.zst"
    fi
    cat > "$TARGET_ROOT/root/.arch-install-chroot.sh" <<'ARCH_CHROOT_SCRIPT'
#!/usr/bin/bash
# This nested script runs inside the newly installed system. / 此内层脚本在新安装的系统中运行。
set -Eeuo pipefail
PATH='/usr/bin'
export PATH
readonly PATH
umask 022

# Generated target-system settings / 自动生成的目标系统设置
