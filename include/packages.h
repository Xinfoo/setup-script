#ifndef ARCH_INSTALLER_PACKAGES_H
#define ARCH_INSTALLER_PACKAGES_H

#include <stdbool.h>
#include <stddef.h>

#define AI_PACKAGE_NAME_LEN 128
#define AI_MAX_PACKAGES_PER_GROUP 128

typedef enum {
    PKG_BOOTSTRAP,
    PKG_CORE,
    PKG_KERNEL_LINUX,
    PKG_KERNEL_LTS,
    PKG_KERNEL_ZEN,
    PKG_KERNEL_HARDENED,
    PKG_PLATFORM_INTEL,
    PKG_PLATFORM_AMD,
    PKG_LAPTOP_FIRMWARE,
    PKG_LAPTOP_TOOLS,
    PKG_GNOME_LAPTOP,
    PKG_INTEL_GRAPHICS,
    PKG_NVIDIA_GRAPHICS,
    PKG_BLUETOOTH,
    PKG_KDE,
    PKG_KDE_RECOMMENDED,
    PKG_FCITX,
    PKG_GNOME,
    PKG_GNOME_RECOMMENDED,
    PKG_IBUS,
    PKG_HYPRLAND,
    PKG_FONTS,
    PKG_FIREWALL,
    PKG_PRINTER,
    PKG_ARCHIVE_TOOLS,
    PKG_TERMINAL_TOOLS,
    PKG_EXTRA_TOOLS,
    PKG_DESKTOP_APPS,
    PKG_SECURE_BOOT_LIVE,
    PKG_GROUP_COUNT
} PackageGroup;

typedef struct {
    char values[AI_MAX_PACKAGES_PER_GROUP][AI_PACKAGE_NAME_LEN];
    size_t count;
} PackageList;

typedef struct {
    unsigned version;
    PackageList groups[PKG_GROUP_COUNT];
} PackageConfig;

void packages_init_defaults(PackageConfig *config);
const char *package_group_name(PackageGroup group);
const PackageList *packages_get(const PackageConfig *config, PackageGroup group);
bool packages_load_json(PackageConfig *config, const char *path,
                        char *error, size_t error_size);
bool packages_save_json(const PackageConfig *config, const char *path,
                        char *error, size_t error_size);

#endif
