# =============================================================================
# Chroot configuration script generation / Chroot 系统配置脚本生成
# =============================================================================

write_chroot_script() {
    # Stage the verified package for Pacman inside the target. / 将已验证的软件包暂存到目标系统供 Pacman 安装。
    if [[ "$ENABLE_SECURE_BOOT" == true ]]; then
        # Only the package crosses into chroot; the private signing key remains in Live tmpfs. / 只有软件包进入 chroot；签名私钥仍留在 Live tmpfs。
        install -m 0600 -- \
            "$SECURE_BOOT_ASSET_SNAPSHOT/shim-signed.pkg.tar.zst" \
            "$TARGET_ROOT/root/.arch-install-shim-signed.pkg.tar.zst"
    fi
    # Generate one self-contained script to run inside the installed system. / 生成一个在目标系统内运行的自包含脚本。
    cat > "$TARGET_ROOT/root/.arch-install-chroot.sh" <<'ARCH_CHROOT_SCRIPT'
#!/usr/bin/bash
# This nested script runs inside the newly installed system. / 此内层脚本在新安装的系统中运行。
set -Eeuo pipefail
# Restrict command lookup and default modes inside the chroot as well. / 在 chroot 内同样限制命令查找路径和默认权限。
PATH='/usr/bin'
export PATH
readonly PATH
umask 022

# Values emitted below are shell-quoted constants captured from the validated plan. / 下方数值是从已验证方案生成并经过 Shell 引用的常量。
# Generated target-system settings / 自动生成的目标系统设置
