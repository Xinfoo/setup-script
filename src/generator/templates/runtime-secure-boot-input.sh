verify_secure_boot_assets() {
    local asset_root=${1:-$ASSET_DIR}
    local file package_info key_public certificate_public crt_fingerprint cer_fingerprint
    [[ "$ENABLE_SECURE_BOOT" == true ]] || return 0
    require_command openssl
    [[ -f "$asset_root/shim-signed.pkg.tar.zst" &&
       ! -L "$asset_root/shim-signed.pkg.tar.zst" ]] ||
        die "Secure Boot requires $asset_root/shim-signed.pkg.tar.zst."
    [[ -d "$asset_root/secure-boot" && ! -L "$asset_root/secure-boot" ]] ||
        die 'The Secure Boot asset directory must be a real directory.'
    for file in MOK.key MOK.crt MOK.cer; do
        [[ -f "$asset_root/secure-boot/$file" && ! -L "$asset_root/secure-boot/$file" ]] ||
            die "Secure Boot asset missing: $asset_root/secure-boot/$file"
    done
    package_info=$(pacman -Qp -- "$asset_root/shim-signed.pkg.tar.zst") ||
        die 'The shim-signed asset is not a readable pacman package.'
    [[ "${package_info%% *}" == shim-signed ]] || die 'The Secure Boot package is not shim-signed.'
    key_public=$(openssl pkey -passin pass: -in "$asset_root/secure-boot/MOK.key" -pubout 2>/dev/null) ||
        die 'MOK.key is invalid or requires a passphrase.'
    certificate_public=$(openssl x509 -in "$asset_root/secure-boot/MOK.crt" -pubkey -noout 2>/dev/null) ||
        die 'MOK.crt is not a valid PEM certificate.'
    [[ "$key_public" == "$certificate_public" ]] || die 'MOK.key and MOK.crt do not match.'
    crt_fingerprint=$(openssl x509 -in "$asset_root/secure-boot/MOK.crt" -noout -fingerprint -sha256 2>/dev/null)
    cer_fingerprint=$(openssl x509 -inform DER -in "$asset_root/secure-boot/MOK.cer" -noout -fingerprint -sha256 2>/dev/null) ||
        die 'MOK.cer is not a valid DER certificate.'
    [[ "$crt_fingerprint" == "$cer_fingerprint" ]] || die 'MOK.crt and MOK.cer do not match.'
}

snapshot_secure_boot_assets() {
    local file archive_path archive_listing
    [[ "$ENABLE_SECURE_BOOT" == true ]] || return 0
    verify_secure_boot_assets "$ASSET_DIR"
    SECURE_BOOT_ASSET_SNAPSHOT=$WORK_DIR/secure-boot-snapshot
    install -d -m 0700 "$SECURE_BOOT_ASSET_SNAPSHOT"
    SECURE_BOOT_SNAPSHOT_MOUNTED=true
    mount -t tmpfs -o nodev,nosuid,noexec,mode=0700,size=64M tmpfs \
        "$SECURE_BOOT_ASSET_SNAPSHOT"
    install -d -m 0700 "$SECURE_BOOT_ASSET_SNAPSHOT/secure-boot"
    install -m 0600 -- "$ASSET_DIR/shim-signed.pkg.tar.zst" \
        "$SECURE_BOOT_ASSET_SNAPSHOT/shim-signed.pkg.tar.zst"
    for file in MOK.key MOK.crt MOK.cer; do
        install -m 0600 -- "$ASSET_DIR/secure-boot/$file" \
            "$SECURE_BOOT_ASSET_SNAPSHOT/secure-boot/$file"
    done
    verify_secure_boot_assets "$SECURE_BOOT_ASSET_SNAPSHOT"
    archive_listing=$(bsdtar -tf "$SECURE_BOOT_ASSET_SNAPSHOT/shim-signed.pkg.tar.zst") ||
        die 'Cannot list the shim-signed package safely.'
    for file in shimx64.efi mmx64.efi fbx64.efi; do
        archive_path=usr/share/shim-signed/$file
        if ! grep -Fxq -- "$archive_path" <<<"$archive_listing"; then
            archive_path=./$archive_path
            grep -Fxq -- "$archive_path" <<<"$archive_listing" ||
                die "shim-signed package is missing $file."
        fi
        bsdtar -xOf "$SECURE_BOOT_ASSET_SNAPSHOT/shim-signed.pkg.tar.zst" \
            "$archive_path" > "$SECURE_BOOT_ASSET_SNAPSHOT/$file" ||
            die "Cannot extract $file from shim-signed safely."
        chmod 0600 "$SECURE_BOOT_ASSET_SNAPSHOT/$file"
        [[ -s "$SECURE_BOOT_ASSET_SNAPSHOT/$file" ]] || die "Extracted $file is empty."
        sbverify --list "$SECURE_BOOT_ASSET_SNAPSHOT/$file" >/dev/null ||
            die "The supplied $file does not contain a valid Secure Boot signature."
    done
}

