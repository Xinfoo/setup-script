atomic_install_file() {
    local source=$1 destination=$2 mode=$3 directory stage stage_index
    directory=${destination%/*}
    [[ -d "$directory" && ! -L "$directory" ]] ||
        die "Secure Boot destination is not a real directory: $directory"
    stage=$(mktemp "$directory/.arch-install-stage.XXXXXX") ||
        die "Cannot stage Secure Boot file in $directory"
    SECURE_BOOT_STAGED_FILES+=("$stage")
    stage_index=$((${#SECURE_BOOT_STAGED_FILES[@]} - 1))
    install -m "$mode" -- "$source" "$stage"
    mv -fT -- "$stage" "$destination"
    SECURE_BOOT_STAGED_FILES[stage_index]=''
}

discard_secure_boot_snapshot() {
    local snapshot_identity
    [[ -n "$SECURE_BOOT_ASSET_SNAPSHOT" ]] || return 0
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
    rmdir -- "$SECURE_BOOT_ASSET_SNAPSHOT" ||
        die 'Cannot remove the private Secure Boot snapshot directory.'
    SECURE_BOOT_ASSET_SNAPSHOT=''
}

sign_secure_boot_assets() {
    local boot_binary kernel_original boot_signed kernel_signed secure_root path
    [[ "$ENABLE_SECURE_BOOT" == true ]] || return 0
    secure_root=$SECURE_BOOT_ASSET_SNAPSHOT
    verify_secure_boot_assets "$secure_root"
    require_command sbsign
    require_command sbverify
    boot_binary=$TARGET_ROOT/boot/EFI/systemd/systemd-bootx64.efi
    kernel_original=$TARGET_ROOT/boot/$KERNEL_IMAGE
    boot_signed=$WORK_DIR/systemd-boot.signed.efi
    kernel_signed=$WORK_DIR/kernel.signed.efi
    [[ -f "$boot_binary" && ! -L "$boot_binary" ]] ||
        die 'Unsigned systemd-boot binary is missing.'
    [[ -f "$kernel_original" && ! -L "$kernel_original" ]] ||
        die 'Unsigned kernel image is missing.'
    for path in "$TARGET_ROOT/boot" "$TARGET_ROOT/boot/EFI" \
                "$TARGET_ROOT/boot/EFI/BOOT" "$TARGET_ROOT/boot/EFI/ARCH"; do
        [[ -d "$path" && ! -L "$path" ]] ||
            die "Secure Boot path is not a real directory: $path"
    done
    phase 'Signing boot files outside the target chroot'
    sbsign --key "$secure_root/secure-boot/MOK.key" \
        --cert "$secure_root/secure-boot/MOK.crt" \
        --output "$boot_signed" "$boot_binary"
    sbsign --key "$secure_root/secure-boot/MOK.key" \
        --cert "$secure_root/secure-boot/MOK.crt" \
        --output "$kernel_signed" "$kernel_original"
    sbverify --cert "$secure_root/secure-boot/MOK.crt" "$boot_signed" >/dev/null
    sbverify --cert "$secure_root/secure-boot/MOK.crt" "$kernel_signed" >/dev/null
    atomic_install_file "$kernel_signed" "$kernel_original" 0644
    atomic_install_file "$boot_signed" \
        "$TARGET_ROOT/boot/EFI/BOOT/GRUBX64.EFI" 0644
    atomic_install_file "$boot_signed" \
        "$TARGET_ROOT/boot/EFI/ARCH/GRUBX64.EFI" 0644
    atomic_install_file "$secure_root/mmx64.efi" \
        "$TARGET_ROOT/boot/EFI/BOOT/MMX64.EFI" 0644
    atomic_install_file "$secure_root/fbx64.efi" \
        "$TARGET_ROOT/boot/EFI/BOOT/FBX64.EFI" 0644
    atomic_install_file "$secure_root/mmx64.efi" \
        "$TARGET_ROOT/boot/EFI/ARCH/MMX64.EFI" 0644
    atomic_install_file "$secure_root/fbx64.efi" \
        "$TARGET_ROOT/boot/EFI/ARCH/FBX64.EFI" 0644
    atomic_install_file "$secure_root/secure-boot/MOK.cer" \
        "$TARGET_ROOT/boot/Arch_Linux_Secure_Boot_Key.cer" 0644
    atomic_install_file "$secure_root/shimx64.efi" \
        "$TARGET_ROOT/boot/EFI/ARCH/SHIMX64.EFI" 0644
    atomic_install_file "$secure_root/shimx64.efi" \
        "$TARGET_ROOT/boot/EFI/BOOT/BOOTX64.EFI" 0644
    SECURE_BOOT_SIGNING_COMPLETE=true
    discard_secure_boot_snapshot
}

