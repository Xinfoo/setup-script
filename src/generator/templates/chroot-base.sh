# =============================================================================
# Base system identity, packages, and drivers / 基础系统身份、软件包与驱动
# =============================================================================

# Resolve the root UUID inside the target immediately before writing loader entries. / 写入启动项前在目标系统内解析根分区 UUID。
ROOT_UUID=$(blkid -s UUID -o value -- "$ROOT_DEVICE") || {
    printf 'Cannot determine the root filesystem UUID.\n' >&2
    exit 1
}
[[ -n "$ROOT_UUID" ]] || { printf 'The root filesystem UUID is empty.\n' >&2; exit 1; }
readonly ROOT_UUID
# Derive every /boot filename from the selected kernel and CPU platform. / 根据所选内核与 CPU 平台推导全部 /boot 文件名。
readonly KERNEL_FILE="vmlinuz-$KERNEL_IMAGE"
readonly INITRAMFS_FILE="initramfs-$KERNEL_IMAGE.img"
readonly MICROCODE_FILE="${MICROCODE_PACKAGE:+$MICROCODE_PACKAGE.img}"

# Install a package list only when it is non-empty. / 仅在软件包列表非空时执行安装。
pacman_install() {
    # Empty optional groups are valid and should not invoke Pacman. / 空的可选软件组合法，不应调用 Pacman。
    (( $# > 0 )) || return 0
    pacman -S --needed --noconfirm "$@"
}

# Limit interactive password retries so installation cannot loop forever. / 限制交互式密码重试次数，避免安装无限循环。
set_account_password() {
    local account=$1 attempt
    # Password entry remains interactive but has a deterministic retry bound. / 密码输入保持交互式，同时具有确定的重试上限。
    for attempt in 1 2 3; do
        if passwd "$account"; then return 0; fi
        printf 'Password update failed for %s (attempt %d of 3).\n' \
            "$account" "$attempt" >&2
    done
    printf 'Giving up after three failed password attempts for %s.\n' "$account" >&2
    return 1
}

# Configure locale, clock, hostname, package database, and root password. / 配置区域、时钟、主机名、软件包数据库和 root 密码。
configure_base() {
    printf '\n==> Configuring locale, clock, and host identity\n'
    # Apply timezone and hardware-clock settings before locale generation. / 生成 Locale 前应用时区与硬件时钟设置。
    [[ -f "/usr/share/zoneinfo/$TIMEZONE" ]] || { printf 'Invalid timezone: %s\n' "$TIMEZONE" >&2; exit 1; }
    ln -sf -- "/usr/share/zoneinfo/$TIMEZONE" /etc/localtime
    hwclock --systohc
    sed -i 's/^#en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/;s/^#zh_CN.UTF-8 UTF-8/zh_CN.UTF-8 UTF-8/' /etc/locale.gen
    locale-gen
    # Replace host identity files with the complete generated content. / 使用生成的完整内容覆盖主机身份文件。
    printf 'LANG=%s\n' "$LOCALE" > /etc/locale.conf
    printf '%s\n' "$HOSTNAME_VALUE" > /etc/hostname
    cat > /etc/hosts <<HOSTS
# Static table lookup for hostnames.
# See hosts(5) for details.

127.0.0.1 localhost
::1 localhost
127.0.1.1 $HOSTNAME_VALUE.localdomain $HOSTNAME_VALUE
HOSTS
    # Keep the console keymap deterministic and enable colored Pacman output. / 固定控制台键盘布局并启用 Pacman 彩色输出。
    printf 'KEYMAP=us\n' > /etc/vconsole.conf
    sed -i 's/^#Color$/Color/' /etc/pacman.conf
    # Refresh against the mirror configuration inherited from pacstrap before installing additions. / 安装附加组件前，使用 pacstrap 继承的镜像配置刷新数据库。
    pacman -Syy --noconfirm
    # Set the root password before creating the ordinary account later. / 在稍后创建普通账户前设置 root 密码。
    printf '\nSet the root password.\n'
    set_account_password root
}

# Install always-required packages and laptop additions. / 安装必需软件包及笔记本附加组件。
install_core_packages() {
    # Core packages are unconditional; laptop tools follow the plan flag. / 核心包始终安装；笔记本工具取决于计划标志。
    pacman_install "${PKG_CORE[@]}"
    [[ "$IS_LAPTOP" != true ]] || pacman_install "${PKG_LAPTOP_TOOLS[@]}"
}

# Install only the hardware support selected in the plan. / 仅安装计划中选择的硬件支持。
install_drivers() {
    # Hardware package groups remain independent and may be combined. / 各硬件软件包组相互独立，可以组合启用。
    if [[ "$INTEL_GRAPHICS" == true ]]; then
        pacman_install "${PKG_INTEL_GRAPHICS[@]}"
    fi
    if [[ "$NVIDIA_GRAPHICS" == true ]]; then
        # Force early NVIDIA modules and remove the conflicting kms hook. / 强制提前加载 NVIDIA 模块并移除冲突的 kms hook。
        sed -i -E 's/^MODULES=.*/MODULES=(nvidia nvidia_modeset nvidia_uvm nvidia_drm)/' /etc/mkinitcpio.conf
        # Remove kms without leaving doubled or edge whitespace. / 移除 kms，同时避免留下连续空格或括号边缘空格。
        # Handle middle, leading, trailing, and sole-element positions independently. / 分别处理 kms 位于中间、开头、末尾及唯一元素的情况。
        sed -i -E '/^HOOKS=/ {
            s/[[:space:]]+kms[[:space:]]+/ /g
            s/\([[:space:]]*kms[[:space:]]+/(/g
            s/[[:space:]]+kms[[:space:]]*\)/)/g
            s/\([[:space:]]*kms[[:space:]]*\)/()/g
        }' /etc/mkinitcpio.conf
        # Install DKMS and userspace components after preparing mkinitcpio. / 准备 mkinitcpio 后安装 DKMS 与用户空间组件。
        pacman_install "${PKG_NVIDIA_GRAPHICS[@]}"
    fi
    if [[ "$HAS_BLUETOOTH" == true ]]; then
        pacman_install "${PKG_BLUETOOTH[@]}"
    fi
}
