# =============================================================================
# Optional UEFI firmware entry / 可选 UEFI 固件启动项
# =============================================================================

create_firmware_entry() {
    local part_number loader label entries boot_partuuid entry
    [[ "$CREATE_EFI_ENTRY" == true ]] || return 0
    # Read the real GPT partition number rather than parsing the device suffix. / 读取真实 GPT 分区号，而不是解析设备名后缀。
    part_number=$(lsblk -dnro PARTN -- "$BOOT_DEVICE")
    [[ "$part_number" =~ ^[0-9]+$ ]] || die 'Cannot determine the EFI partition number.'
    # Keep the firmware-visible name identical in normal and Secure Boot modes. / 普通模式与 Secure Boot 模式使用相同的固件显示名称。
    label='Linux Boot Manager'
    # Only the loader path changes with the selected boot chain. / 只有加载器路径随所选启动链变化。
    if [[ "$ENABLE_SECURE_BOOT" == true ]]; then
        loader='\EFI\ARCH\SHIMX64.EFI'
    else
        loader='\EFI\systemd\systemd-bootx64.efi'
    fi
    # Bind duplicate detection to the EFI filesystem's stable partition UUID. / 使用 EFI 文件系统的稳定分区 UUID 进行重复项检测。
    boot_partuuid=$(lsblk -dnro PARTUUID -- "$BOOT_DEVICE") ||
        die 'Cannot determine the EFI partition PARTUUID.'
    [[ -n "$boot_partuuid" ]] || die 'The EFI partition has no PARTUUID.'
    entries=$(arch-chroot "$TARGET_ROOT" efibootmgr -v) ||
        die 'Cannot read existing EFI boot entries.'
    # Match label, partition, and loader together before deciding to reuse an entry. / 同时匹配名称、分区和加载器后才复用已有项。
    while IFS= read -r entry; do
        if [[ "$entry" == *"$label"* &&
              "${entry,,}" == *"${boot_partuuid,,}"* &&
              "${entry,,}" == *"${loader,,}"* ]]; then
            printf 'The matching EFI entry already exists; not creating a duplicate.\n'
            return 0
        fi
    done <<<"$entries"
    # Create the entry through target efibootmgr with the resolved disk and partition. / 使用已解析的磁盘和分区通过目标 efibootmgr 创建启动项。
    arch-chroot "$TARGET_ROOT" efibootmgr --create --disk "$TARGET_DISK" \
        --part "$part_number" --loader "$loader" --label "$label" --unicode
}
