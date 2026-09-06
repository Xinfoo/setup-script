# =============================================================================
# Secure Boot signing and atomic installation / Secure Boot 签名与原子安装
# =============================================================================

# Stage each signed file beside its destination before replacing it. / 在目标旁暂存每个签名文件，再执行替换。
atomic_install_file() {
    local source=$1 destination=$2 mode=$3 directory stage stage_index
    # Stage on the destination filesystem so the final rename is atomic. / 在目标文件系统上暂存，使最终重命名具备原子性。
    directory=${destination%/*}
    [[ -d "$directory" && ! -L "$directory" ]] ||
        die "Secure Boot destination is not a real directory: $directory"
    stage=$(mktemp "$directory/.arch-install-stage.XXXXXX") ||
        die "Cannot stage Secure Boot file in $directory"
    # Register the stage before copying so EXIT cleanup can remove partial files. / 复制前登记暂存文件，使 EXIT 清理能删除不完整文件。
    SECURE_BOOT_STAGED_FILES+=("$stage")
    stage_index=$((${#SECURE_BOOT_STAGED_FILES[@]} - 1))
    install -m "$mode" -- "$source" "$stage"
    # Commit the completed file, then clear its cleanup record. / 提交完整文件后清除对应清理记录。
    # mv -T treats the destination as one path and never descends into an unexpected directory. / mv -T 将目标视为单一路径，不会进入意外出现的目录。
    mv -fT -- "$stage" "$destination"
    SECURE_BOOT_STAGED_FILES[stage_index]=''
}

# Unmount and remove the private key snapshot after signing. / 签名完成后卸载并移除私钥快照。
discard_secure_boot_snapshot() {
    local snapshot_identity
    [[ -n "$SECURE_BOOT_ASSET_SNAPSHOT" ]] || return 0
    # Unmount only when the path is still the tmpfs created by this run. / 仅当路径仍为本次创建的 tmpfs 时才卸载。
    if [[ "$SECURE_BOOT_SNAPSHOT_MOUNTED" == true ]]; then
        snapshot_identity=$(findmnt -rn --mountpoint \
            "$SECURE_BOOT_ASSET_SNAPSHOT" -o SOURCE,FSTYPE) ||
            die 'Cannot verify the private Secure Boot snapshot mount.'
        [[ "$snapshot_identity" == 'tmpfs tmpfs' ]] ||
            die 'Refusing to unmount an unexpected Secure Boot snapshot source.'
        umount -- "$SECURE_BOOT_ASSET_SNAPSHOT" ||
            die 'Cannot unmount the private Secure Boot snapshot.'
        SECURE_BOOT_SNAPSHOT_MOUNTED=false
    fi
    # The mountpoint must be empty after the tmpfs is released. / tmpfs 释放后挂载点必须为空。
    rmdir -- "$SECURE_BOOT_ASSET_SNAPSHOT" ||
        die 'Cannot remove the private Secure Boot snapshot directory.'
    SECURE_BOOT_ASSET_SNAPSHOT=''
}

# Sign and verify boot artifacts outside the target chroot. / 在目标 chroot 外签名并验证启动文件。
sign_secure_boot_assets() {
    local boot_binary kernel_original boot_signed kernel_signed secure_root path
    [[ "$ENABLE_SECURE_BOOT" == true ]] || return 0
    # Revalidate the private snapshot immediately before private-key use. / 使用私钥前立即再次校验私有快照。
    secure_root=$SECURE_BOOT_ASSET_SNAPSHOT
    verify_secure_boot_assets "$secure_root"
    require_command sbsign
    require_command sbverify
    boot_binary=$TARGET_ROOT/boot/EFI/systemd/systemd-bootx64.efi
    kernel_original=$TARGET_ROOT/boot/$KERNEL_IMAGE
    boot_signed=$WORK_DIR/systemd-boot.signed.efi
    kernel_signed=$WORK_DIR/kernel.signed.efi
    # Sign only ordinary source files installed by the target package manager. / 仅签署目标包管理器安装的普通源文件。
    [[ -f "$boot_binary" && ! -L "$boot_binary" ]] ||
        die 'Unsigned systemd-boot binary is missing.'
    [[ -f "$kernel_original" && ! -L "$kernel_original" ]] ||
        die 'Unsigned kernel image is missing.'
    for path in "$TARGET_ROOT/boot" "$TARGET_ROOT/boot/EFI" \
                "$TARGET_ROOT/boot/EFI/BOOT" "$TARGET_ROOT/boot/EFI/ARCH"; do
        # Destination directories must not redirect writes through symlinks. / 目标目录不得通过符号链接重定向写入。
        [[ -d "$path" && ! -L "$path" ]] ||
            die "Secure Boot path is not a real directory: $path"
    done
    phase 'Signing boot files outside the target chroot'
    # Produce signed boot manager and kernel copies in the private workspace. / 在私有工作目录生成已签名的引导管理器和内核副本。
    sbsign --key "$secure_root/secure-boot/MOK.key" \
        --cert "$secure_root/secure-boot/MOK.crt" \
        --output "$boot_signed" "$boot_binary"
    sbsign --key "$secure_root/secure-boot/MOK.key" \
        --cert "$secure_root/secure-boot/MOK.crt" \
        --output "$kernel_signed" "$kernel_original"
    # Verify both outputs against the selected MOK before committing either one. / 提交任何文件前用所选 MOK 校验两个输出。
    sbverify --cert "$secure_root/secure-boot/MOK.crt" "$boot_signed" >/dev/null
    sbverify --cert "$secure_root/secure-boot/MOK.crt" "$kernel_signed" >/dev/null
    # Only after both verifications succeed may either destination be replaced. / 只有两项验证都成功后才允许替换任一目标文件。
    # Keep the original unsigned kernel as a manual recovery image. / 保留原始未签名内核，供手工恢复使用。
    atomic_install_file "$kernel_original" "$kernel_original.bak" 0644
    atomic_install_file "$kernel_signed" "$kernel_original" 0644
    # Install signed systemd-boot under shim's expected GRUB filename. / 以 shim 预期的 GRUB 文件名安装已签名 systemd-boot。
    atomic_install_file "$boot_signed" \
        "$TARGET_ROOT/boot/EFI/BOOT/GRUBX64.EFI" 0644
    atomic_install_file "$boot_signed" \
        "$TARGET_ROOT/boot/EFI/ARCH/GRUBX64.EFI" 0644
    # Mirror MokManager and fallback manager into both boot paths. / 将 MokManager 与 fallback manager 同步到两条启动路径。
    atomic_install_file "$secure_root/mmx64.efi" \
        "$TARGET_ROOT/boot/EFI/BOOT/MMX64.EFI" 0644
    atomic_install_file "$secure_root/fbx64.efi" \
        "$TARGET_ROOT/boot/EFI/BOOT/FBX64.EFI" 0644
    atomic_install_file "$secure_root/mmx64.efi" \
        "$TARGET_ROOT/boot/EFI/ARCH/MMX64.EFI" 0644
    atomic_install_file "$secure_root/fbx64.efi" \
        "$TARGET_ROOT/boot/EFI/ARCH/FBX64.EFI" 0644
    # Expose only the public enrollment certificate on the EFI partition. / EFI 分区上只公开用于注册的公钥证书。
    atomic_install_file "$secure_root/secure-boot/MOK.cer" \
        "$TARGET_ROOT/boot/Arch_Linux_Secure_Boot_Key.cer" 0644
    atomic_install_file "$secure_root/shimx64.efi" \
        "$TARGET_ROOT/boot/EFI/ARCH/SHIMX64.EFI" 0644
    atomic_install_file "$secure_root/shimx64.efi" \
        "$TARGET_ROOT/boot/EFI/BOOT/BOOTX64.EFI" 0644
    # Mark completion before destroying the only in-memory private-key snapshot. / 销毁唯一的内存私钥快照前标记签名完成。
    SECURE_BOOT_SIGNING_COMPLETE=true
    discard_secure_boot_snapshot
}
