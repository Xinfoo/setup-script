#include "../private.h"

/* 目标系统内部执行的固定逻辑按配置领域拆分，并在构建期嵌入。 */
static const unsigned char chroot_preamble_template[] = {
#include "generated/generator/chroot_preamble.inc"
};

static const unsigned char chroot_base_template[] = {
#include "generated/generator/chroot_base.inc"
};

static const unsigned char chroot_desktop_template[] = {
#include "generated/generator/chroot_desktop.inc"
};

static const unsigned char chroot_optional_software_template[] = {
#include "generated/generator/chroot_optional_software.inc"
};

static const unsigned char chroot_bootloader_template[] = {
#include "generated/generator/chroot_bootloader.inc"
};

static const unsigned char chroot_system_template[] = {
#include "generated/generator/chroot_system.inc"
};

/*
 * chroot 前导之后先写入本次方案的动态参数和软件包数组，再依次拼接
 * 基础系统、桌面、可选软件、引导器及系统服务配置。
 */
bool emit_chroot_configuration(ScriptWriter *writer, const InstallPlan *plan,
                               const PackageConfig *packages)
{
    const char *desktop;

    switch (plan->system.desktop) {
    case DESKTOP_KDE:
        desktop = "kde";
        break;
    case DESKTOP_GNOME:
        desktop = "gnome";
        break;
    case DESKTOP_HYPRLAND:
        desktop = "hyprland";
        break;
    case DESKTOP_NONE:
        desktop = "none";
        break;
    default:
        desktop = "none";
        break;
    }

    writer_write(writer, chroot_preamble_template, sizeof(chroot_preamble_template));
    if (!emit_assignment(writer, "TIMEZONE", plan->system.timezone) ||
        !emit_assignment(writer, "LOCALE", locale_name(plan->system.locale)) ||
        !emit_assignment(writer, "HOSTNAME_VALUE", plan->system.hostname) ||
        !emit_assignment(writer, "USERNAME", plan->system.username) ||
        !emit_assignment(writer, "ROOT_DEVICE", find_partition(plan, PART_ROOT)->device) ||
        !emit_assignment(writer, "KERNEL_IMAGE", kernel_name(plan->system.kernel)) ||
        !emit_assignment(writer, "MICROCODE_PACKAGE",
                         plan->system.platform == PLATFORM_INTEL
                             ? "intel-ucode"
                             : plan->system.platform == PLATFORM_AMD ? "amd-ucode" : "") ||
        !emit_assignment(writer, "DESKTOP", desktop)) {
        return false;
    }
    emit_boolean(writer, "IS_LAPTOP", plan->system.laptop);
    emit_boolean(writer, "INTEL_GRAPHICS", plan->system.intel_graphics);
    emit_boolean(writer, "NVIDIA_GRAPHICS", plan->system.nvidia_graphics);
    emit_boolean(writer, "HAS_BLUETOOTH", plan->system.bluetooth);
    emit_boolean(writer, "DESKTOP_RECOMMENDED", plan->system.desktop_recommended);
    emit_boolean(writer, "CHINESE_INPUT", plan->system.chinese_input);
    emit_boolean(writer, "ENABLE_FIREWALL", plan->system.firewall);
    emit_boolean(writer, "ENABLE_PRINTER", plan->system.printer);
    emit_boolean(writer, "INSTALL_ARCHIVE_TOOLS", plan->system.archive_tools);
    emit_boolean(writer, "INSTALL_TERMINAL_TOOLS", plan->system.terminal_tools);
    emit_boolean(writer, "INSTALL_EXTRA_TOOLS", plan->system.extra_tools);
    emit_boolean(writer, "INSTALL_DESKTOP_APPS", plan->system.desktop_apps);
    emit_boolean(writer, "USE_CHINA_MIRRORS", plan->system.china_mirrors);
    emit_boolean(writer, "ENABLE_SECURE_BOOT", plan->system.secure_boot);
    emit_package_array(writer, "PKG_CORE", packages, PKG_CORE);
    emit_package_array(writer, "PKG_LAPTOP_TOOLS", packages, PKG_LAPTOP_TOOLS);
    emit_package_array(writer, "PKG_GNOME_LAPTOP", packages, PKG_GNOME_LAPTOP);
    emit_package_array(writer, "PKG_INTEL_GRAPHICS", packages, PKG_INTEL_GRAPHICS);
    emit_package_array(writer, "PKG_NVIDIA_GRAPHICS", packages, PKG_NVIDIA_GRAPHICS);
    emit_package_array(writer, "PKG_BLUETOOTH", packages, PKG_BLUETOOTH);
    emit_package_array(writer, "PKG_KDE", packages, PKG_KDE);
    emit_package_array(writer, "PKG_KDE_RECOMMENDED", packages, PKG_KDE_RECOMMENDED);
    emit_package_array(writer, "PKG_FCITX", packages, PKG_FCITX);
    emit_package_array(writer, "PKG_GNOME", packages, PKG_GNOME);
    emit_package_array(writer, "PKG_GNOME_RECOMMENDED", packages, PKG_GNOME_RECOMMENDED);
    emit_package_array(writer, "PKG_IBUS", packages, PKG_IBUS);
    emit_package_array(writer, "PKG_HYPRLAND", packages, PKG_HYPRLAND);
    emit_package_array(writer, "PKG_FONTS", packages, PKG_FONTS);
    emit_package_array(writer, "PKG_FIREWALL", packages, PKG_FIREWALL);
    emit_package_array(writer, "PKG_PRINTER", packages, PKG_PRINTER);
    emit_package_array(writer, "PKG_ARCHIVE_TOOLS", packages, PKG_ARCHIVE_TOOLS);
    emit_package_array(writer, "PKG_TERMINAL_TOOLS", packages, PKG_TERMINAL_TOOLS);
    emit_package_array(writer, "PKG_EXTRA_TOOLS", packages, PKG_EXTRA_TOOLS);
    emit_package_array(writer, "PKG_DESKTOP_APPS", packages, PKG_DESKTOP_APPS);
    writer_write(writer, chroot_base_template, sizeof(chroot_base_template));
    writer_write(writer, chroot_desktop_template, sizeof(chroot_desktop_template));
    writer_write(writer, chroot_optional_software_template,
                 sizeof(chroot_optional_software_template));
    writer_write(writer, chroot_bootloader_template, sizeof(chroot_bootloader_template));
    writer_write(writer, chroot_system_template, sizeof(chroot_system_template));
    return writer->ok;
}
