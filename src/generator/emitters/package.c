#include "../private.h"

#include <stdlib.h>

/* 软件包数组中的每个名称单独转义，缺失的软件包组会让整个生成过程失败。 */
static void emit_package_values(ScriptWriter *writer, const PackageList *packages)
{
    for (size_t index = 0; index < packages->count; ++index) {
        char *quoted = shell_quote(packages->values[index]);
        if (quoted == NULL) {
            writer->ok = false;
            return;
        }
        writer_printf(writer, "    %s\n", quoted);
        free(quoted);
    }
}

static void emit_package_group(ScriptWriter *writer, const PackageConfig *config,
                               PackageGroup group)
{
    const PackageList *packages = packages_get(config, group);

    if (packages == NULL) {
        writer->ok = false;
        return;
    }
    emit_package_values(writer, packages);
}

void emit_package_array(ScriptWriter *writer, const char *name,
                               const PackageConfig *config, PackageGroup group)
{
    writer_printf(writer, "%s=(\n", name);
    emit_package_group(writer, config, group);
    writer_puts(writer, ")\n");
}

/* REQUIRED_PACKAGES 是所有已启用功能的软件包并集，供安装前统一解析验证。 */
static void emit_required_packages(ScriptWriter *writer, const InstallPlan *plan,
                                   const PackageConfig *config)
{
    PackageGroupList groups;

    packages_collect_required_groups(plan, &groups);
    writer_puts(writer, "REQUIRED_PACKAGES=(\n");
    for (size_t index = 0; index < groups.count; ++index) {
        emit_package_group(writer, config, groups.values[index]);
    }
    writer_puts(writer, ")\n");
}

void emit_live_package_plan(ScriptWriter *writer, const InstallPlan *plan,
                            const PackageConfig *packages)
{
    emit_package_array(writer, "BOOTSTRAP_PACKAGES", packages, PKG_BOOTSTRAP);
    emit_package_array(writer, "KERNEL_PACKAGES", packages,
                       packages_kernel_group(plan->system.kernel));
    {
        PackageGroup platform_group;
        if (packages_platform_group(plan->system.platform, &platform_group)) {
            emit_package_array(writer, "PLATFORM_PACKAGES", packages, platform_group);
        } else {
            /* 虚拟机平台没有额外微码包，但仍输出空数组以保持模板接口稳定。 */
            writer_puts(writer, "PLATFORM_PACKAGES=(\n)\n");
        }
    }
    emit_package_array(writer, "LAPTOP_FIRMWARE_PACKAGES", packages,
                       PKG_LAPTOP_FIRMWARE);
    emit_package_array(writer, "LOCAL_MIRROR_LIVE_PACKAGES", packages,
                       PKG_LOCAL_MIRROR_LIVE);
    emit_package_array(writer, "LIVE_SIGNING_PACKAGES", packages,
                       PKG_SECURE_BOOT_LIVE);
    emit_required_packages(writer, plan, packages);
}
