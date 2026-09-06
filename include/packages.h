#ifndef ARCH_INSTALLER_PACKAGES_H
#define ARCH_INSTALLER_PACKAGES_H

#include "model.h"

#include <stdbool.h>
#include <stddef.h>

#define AI_PACKAGE_NAME_LEN 128
#define AI_MAX_PACKAGES_PER_GROUP 128

/* 安装阶段和可选功能使用的软件包组；枚举值也是配置表的稳定索引。 */
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
    PKG_LOCAL_MIRROR_LIVE,
    PKG_SECURE_BOOT_LIVE,
    PKG_GROUP_COUNT
} PackageGroup;

/* 每组使用固定上限，配置加载完成后可直接由生成器只读遍历。 */
typedef struct {
    char values[AI_MAX_PACKAGES_PER_GROUP][AI_PACKAGE_NAME_LEN];
    size_t count;
} PackageList;

typedef struct {
    PackageList groups[PKG_GROUP_COUNT];
} PackageConfig;

/* 已启用功能对应的软件包组列表；固定容量覆盖全部已知组且不进行动态分配。 */
typedef struct {
    PackageGroup values[PKG_GROUP_COUNT];
    size_t count;
} PackageGroupList;

/* 初始化内建默认配置，并提供软件包组的名称和只读访问接口。 */
void packages_init_defaults(PackageConfig *config);
const char *package_group_name(PackageGroup group);
const PackageList *packages_get(const PackageConfig *config, PackageGroup group);

/*
 * 选项到软件包组的唯一映射，同时供 TUI 预览和脚本生成器使用。
 * bool 接口返回 false 表示该选项没有附加包，例如 VM、Desktop None。
 */
PackageGroup packages_kernel_group(Kernel kernel);
bool packages_platform_group(Platform platform, PackageGroup *group);
bool packages_desktop_group(Desktop desktop, PackageGroup *group);
bool packages_desktop_recommended_group(Desktop desktop, PackageGroup *group);
bool packages_input_group(Desktop desktop, PackageGroup *group);

/* 按脚本预解析所需的稳定顺序收集当前完整安装方案启用的全部软件包组。 */
void packages_collect_required_groups(const InstallPlan *plan, PackageGroupList *groups);

/* JSON 接口执行完整格式校验；保存使用临时文件原子替换目标。 */
bool packages_load_json(PackageConfig *config, const char *path,
                        char *error, size_t error_size);
bool packages_save_json(const PackageConfig *config, const char *path,
                        char *error, size_t error_size);

#endif
