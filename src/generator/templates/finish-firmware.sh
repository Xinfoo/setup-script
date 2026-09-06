# =============================================================================
# Optional UEFI firmware entry / 可选 UEFI 固件启动项
# =============================================================================

create_firmware_entry() {
    local part_number loader label entries boot_partuuid entry
    [[ "$CREATE_EFI_ENTRY" == true ]] || return 0
    part_number=$(lsblk -dnro PARTN -- "$BOOT_DEVICE")
    [[ "$part_number" =~ ^[0-9]+$ ]] || die 'Cannot determine the EFI partition number.'
    label='Linux Boot Manager'
    if [[ "$ENABLE_SECURE_BOOT" == true ]]; then
        loader='\EFI\ARCH\SHIMX64.EFI'
    else
        loader='\EFI\systemd\systemd-bootx64.efi'
    fi
    boot_partuuid=$(lsblk -dnro PARTUUID -- "$BOOT_DEVICE") ||
        die 'Cannot determine the EFI partition PARTUUID.'
    [[ -n "$boot_partuuid" ]] || die 'The EFI partition has no PARTUUID.'
    entries=$(arch-chroot "$TARGET_ROOT" efibootmgr -v) ||
        die 'Cannot read existing EFI boot entries.'
    while IFS= read -r entry; do
        if [[ "$entry" == *"$label"* &&
              "${entry,,}" == *"${boot_partuuid,,}"* &&
              "${entry,,}" == *"${loader,,}"* ]]; then
            printf 'The matching EFI entry already exists; not creating a duplicate.\n'
            return 0
        fi
    done <<<"$entries"
    arch-chroot "$TARGET_ROOT" efibootmgr --create --disk "$TARGET_DISK" \
        --part "$part_number" --loader "$loader" --label "$label" --unicode
}
