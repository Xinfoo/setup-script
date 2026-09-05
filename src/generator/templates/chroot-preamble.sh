write_chroot_script() {
    cat > "$TARGET_ROOT/root/.arch-install-chroot.sh" <<'ARCH_CHROOT_SCRIPT'
#!/usr/bin/bash
set -Eeuo pipefail
PATH='/usr/bin'
export PATH
readonly PATH
umask 022

