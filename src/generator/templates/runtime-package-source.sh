prepare_package_source() {
    if [[ "$USE_LOCAL_MIRROR" == true ]]; then
        setup_local_mirror
    else
        phase 'Checking package repositories'
        pacman -Syy --noconfirm
    fi
    if [[ "$ENABLE_SECURE_BOOT" == true ]]; then
        phase 'Installing the Live signing tool'
        (( ${#LIVE_SIGNING_PACKAGES[@]} > 0 )) ||
            die 'The Secure Boot Live package group is empty.'
        pacman -S --needed --noconfirm "${LIVE_SIGNING_PACKAGES[@]}"
        require_command sbsign
        require_command sbverify
        require_command bsdtar
    fi
    phase 'Resolving the complete package selection'
    pacman -Sp --needed --noconfirm "${REQUIRED_PACKAGES[@]}" >/dev/null ||
        die 'One or more selected packages cannot be resolved before installation.'
}

install_base_system() {
    local packages=("${BOOTSTRAP_PACKAGES[@]}" "${KERNEL_PACKAGES[@]}" "${PLATFORM_PACKAGES[@]}")
    [[ "$IS_LAPTOP" != true ]] || packages+=("${LAPTOP_FIRMWARE_PACKAGES[@]}")
    (( ${#packages[@]} > 0 )) || die 'The bootstrap package selection is empty.'
    phase 'Installing the base system'
    pacstrap -K "$TARGET_ROOT" "${packages[@]}"
    genfstab -U "$TARGET_ROOT" > "$TARGET_ROOT/etc/fstab"
    if [[ "$USE_LOCAL_MIRROR" == true ]]; then
        cp -a "$TARGET_ROOT/etc/pacman.conf" "$WORK_DIR/target-pacman.conf"
        cp -a "$TARGET_ROOT/etc/pacman.d/mirrorlist" "$WORK_DIR/target-mirrorlist"
        setup_target_local_mirror
        sed -i -E 's/^[[:space:]#]*SigLevel[[:space:]]*=.*/SigLevel = Never/' "$TARGET_ROOT/etc/pacman.conf"
        printf '%s\n' 'Server = file:///var/cache/arch-install-repo/$repo/os/$arch' > "$TARGET_ROOT/etc/pacman.d/mirrorlist"
    fi
}
