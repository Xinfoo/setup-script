# =============================================================================
# Secure Boot input validation and private snapshot / Secure Boot 输入校验与私有快照
# =============================================================================

verify_secure_boot_assets() {
    local asset_root=${1:-$ASSET_DIR}
    local file package_info key_public certificate_public crt_fingerprint cer_fingerprint
    [[ "$ENABLE_SECURE_BOOT" == true ]] || return 0
    # Validation establishes structural consistency, not publisher authenticity or package trust. / 校验只建立结构一致性，不代表认证发布者身份或软件包可信性。
    # Inputs must be ordinary files so later reads cannot follow substituted links. / 输入必须是普通文件，防止后续读取跟随被替换的链接。
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
    # Confirm that the supplied archive is the expected Pacman package. / 确认所提供归档确实是预期的 Pacman 软件包。
    package_info=$(pacman -Qp -- "$asset_root/shim-signed.pkg.tar.zst") ||
        die 'The shim-signed asset is not a readable pacman package.'
    [[ "${package_info%% *}" == shim-signed ]] || die 'The Secure Boot package is not shim-signed.'
    # Compare public keys before any code is signed. / 在签署任何代码前比较公钥。
    key_public=$(openssl pkey -passin pass: -in "$asset_root/secure-boot/MOK.key" -pubout 2>/dev/null) ||
        die 'MOK.key is invalid or requires a passphrase.'
    certificate_public=$(openssl x509 -in "$asset_root/secure-boot/MOK.crt" -pubkey -noout 2>/dev/null) ||
        die 'MOK.crt is not a valid PEM certificate.'
    [[ "$key_public" == "$certificate_public" ]] || die 'MOK.key and MOK.crt do not match.'
    # PEM and DER certificates must encode the same enrollment identity. / PEM 与 DER 证书必须表示同一个注册身份。
    crt_fingerprint=$(openssl x509 -in "$asset_root/secure-boot/MOK.crt" -noout -fingerprint -sha256 2>/dev/null)
    cer_fingerprint=$(openssl x509 -inform DER -in "$asset_root/secure-boot/MOK.cer" -noout -fingerprint -sha256 2>/dev/null) ||
        die 'MOK.cer is not a valid DER certificate.'
    [[ "$crt_fingerprint" == "$cer_fingerprint" ]] || die 'MOK.crt and MOK.cer do not match.'
}

# Copy verified keys and shim files into a private tmpfs snapshot. / 将已验证的密钥和 shim 文件复制到私有 tmpfs 快照。
snapshot_secure_boot_assets() {
    local file archive_path archive_listing
    [[ "$ENABLE_SECURE_BOOT" == true ]] || return 0
    # Validate mutable source material before taking the private snapshot. / 创建私有快照前先校验可变的源材料。
    verify_secure_boot_assets "$ASSET_DIR"
    SECURE_BOOT_ASSET_SNAPSHOT=$WORK_DIR/secure-boot-snapshot
    install -d -m 0700 "$SECURE_BOOT_ASSET_SNAPSHOT"
    # Publish mount ownership before mount so cleanup can inspect an interrupted attempt. / 在挂载前登记所有权，使清理逻辑能检查被中断的尝试。
    SECURE_BOOT_SNAPSHOT_MOUNTED=true
    # tmpfs prevents the private key from being written into the target system. / tmpfs 防止私钥写入目标系统。
    mount -t tmpfs -o nodev,nosuid,noexec,mode=0700,size=64M tmpfs \
        "$SECURE_BOOT_ASSET_SNAPSHOT"
    install -d -m 0700 "$SECURE_BOOT_ASSET_SNAPSHOT/secure-boot"
    install -m 0600 -- "$ASSET_DIR/shim-signed.pkg.tar.zst" \
        "$SECURE_BOOT_ASSET_SNAPSHOT/shim-signed.pkg.tar.zst"
    for file in MOK.key MOK.crt MOK.cer; do
        install -m 0600 -- "$ASSET_DIR/secure-boot/$file" \
            "$SECURE_BOOT_ASSET_SNAPSHOT/secure-boot/$file"
    done
    # Revalidate the copied snapshot before extracting executable payloads. / 提取可执行文件前再次校验复制后的快照。
    verify_secure_boot_assets "$SECURE_BOOT_ASSET_SNAPSHOT"
    archive_listing=$(bsdtar -tf "$SECURE_BOOT_ASSET_SNAPSHOT/shim-signed.pkg.tar.zst") ||
        die 'Cannot list the shim-signed package safely.'
    # Accept both Pacman archive path spellings, but only for the three required files. / 兼容 Pacman 归档的两种路径写法，但仅提取三个必需文件。
    for file in shimx64.efi mmx64.efi fbx64.efi; do
        archive_path=usr/share/shim-signed/$file
        if ! grep -Fxq -- "$archive_path" <<<"$archive_listing"; then
            # Some Pacman archives prefix member names with ./ while others do not. / 不同 Pacman 归档可能会给成员名加上或省略 ./ 前缀。
            archive_path=./$archive_path
            grep -Fxq -- "$archive_path" <<<"$archive_listing" ||
                die "shim-signed package is missing $file."
        fi
        bsdtar -xOf "$SECURE_BOOT_ASSET_SNAPSHOT/shim-signed.pkg.tar.zst" \
            "$archive_path" > "$SECURE_BOOT_ASSET_SNAPSHOT/$file" ||
            die "Cannot extract $file from shim-signed safely."
        chmod 0600 "$SECURE_BOOT_ASSET_SNAPSHOT/$file"
        # Reject empty or unsigned EFI payloads before disk modification. / 磁盘修改前拒绝空文件或未签名的 EFI 载荷。
        [[ -s "$SECURE_BOOT_ASSET_SNAPSHOT/$file" ]] || die "Extracted $file is empty."
        sbverify --list "$SECURE_BOOT_ASSET_SNAPSHOT/$file" >/dev/null ||
            die "The supplied $file does not contain a valid Secure Boot signature."
    done
}
