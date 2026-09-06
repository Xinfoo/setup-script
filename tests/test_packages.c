#define _POSIX_C_SOURCE 200809L

#include "packages.h"
#include "util.h"

#include <json-c/json.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* 方案映射测试锁定跨阶段包组顺序，并覆盖没有附加组的桌面分支。 */
static bool package_group_selection_matches_plan(void)
{
    static const PackageGroup expected[] = {
        PKG_BOOTSTRAP, PKG_CORE, PKG_KERNEL_ZEN, PKG_PLATFORM_AMD,
        PKG_LAPTOP_FIRMWARE, PKG_LAPTOP_TOOLS, PKG_INTEL_GRAPHICS,
        PKG_NVIDIA_GRAPHICS, PKG_BLUETOOTH, PKG_GNOME, PKG_GNOME_LAPTOP,
        PKG_GNOME_RECOMMENDED, PKG_IBUS, PKG_FONTS, PKG_FIREWALL,
        PKG_PRINTER, PKG_ARCHIVE_TOOLS, PKG_TERMINAL_TOOLS, PKG_EXTRA_TOOLS,
        PKG_DESKTOP_APPS, PKG_LOCAL_MIRROR_LIVE, PKG_SECURE_BOOT_LIVE
    };
    InstallPlan plan = {0};
    PackageGroupList groups;
    PackageGroup group;

    plan.system.platform = PLATFORM_AMD;
    plan.system.kernel = KERNEL_ZEN;
    plan.system.desktop = DESKTOP_GNOME;
    plan.system.laptop = true;
    plan.system.intel_graphics = true;
    plan.system.nvidia_graphics = true;
    plan.system.bluetooth = true;
    plan.system.desktop_recommended = true;
    plan.system.chinese_input = true;
    plan.system.firewall = true;
    plan.system.printer = true;
    plan.system.archive_tools = true;
    plan.system.terminal_tools = true;
    plan.system.extra_tools = true;
    plan.system.desktop_apps = true;
    plan.system.local_mirror = true;
    plan.system.secure_boot = true;
    packages_collect_required_groups(&plan, &groups);

    if (groups.count != sizeof(expected) / sizeof(expected[0]) ||
        memcmp(groups.values, expected, sizeof(expected)) != 0) return false;
    if (!packages_desktop_group(DESKTOP_HYPRLAND, &group) ||
        group != PKG_HYPRLAND ||
        !packages_input_group(DESKTOP_HYPRLAND, &group) || group != PKG_FCITX ||
        packages_desktop_recommended_group(DESKTOP_HYPRLAND, &group) ||
        packages_desktop_group(DESKTOP_NONE, &group) ||
        packages_input_group(DESKTOP_NONE, &group)) return false;
    return true;
}

/* 在独立临时目录中同时验证默认配置往返和缺失软件包组的严格拒绝。 */
int main(void)
{
    char directory[] = "/tmp/arch-install-packages-test-XXXXXX";
    char path[256];
    char error[256] = {0};
    PackageConfig defaults;
    PackageConfig loaded;
    struct json_object *root = NULL;
    struct json_object *groups = NULL;
    struct stat status;
    bool passed = true;

    if (!package_group_selection_matches_plan()) {
        (void)fprintf(stderr, "package group selection no longer matches plan options\n");
        return EXIT_FAILURE;
    }

    /* 先保存再加载默认值，确认较大软件包组和普通组均保持不变。 */
    if (mkdtemp(directory) == NULL) return EXIT_FAILURE;
    (void)snprintf(path, sizeof(path), "%s/packages.json", directory);
    packages_init_defaults(&defaults);
    if (!packages_save_json(&defaults, path, error, sizeof(error)) ||
        !packages_load_json(&loaded, path, error, sizeof(error))) {
        (void)fprintf(stderr, "package config round trip failed: %s\n", error);
        passed = false;
        goto finish;
    }
    if (stat(path, &status) != 0 || (status.st_mode & 0777) != 0644) {
        (void)fprintf(stderr, "package config permissions are not 0644\n");
        passed = false;
        goto finish;
    }
    {
        FILE *file = fopen(path, "rb");
        int last = EOF;
        if (file != NULL && fseek(file, -1, SEEK_END) == 0) last = fgetc(file);
        if (file != NULL) (void)fclose(file);
        if (last != '\n') {
            (void)fprintf(stderr, "package config lost its trailing newline\n");
            passed = false;
            goto finish;
        }
    }
    if (loaded.groups[PKG_LOCAL_MIRROR_LIVE].count != 1 ||
        strcmp(loaded.groups[PKG_LOCAL_MIRROR_LIVE].values[0], "nginx") != 0 ||
        loaded.groups[PKG_HYPRLAND].count != defaults.groups[PKG_HYPRLAND].count ||
        strcmp(loaded.groups[PKG_CORE].values[0],
               defaults.groups[PKG_CORE].values[0]) != 0) {
        (void)fprintf(stderr, "package config changed during round trip\n");
        passed = false;
        goto finish;
    }

    /* 删除一个必需组后重写配置，加载器必须指出具体缺失组。 */
    root = json_object_from_file(path);
    if (root == NULL || !json_object_object_get_ex(root, "groups", &groups)) {
        passed = false;
        goto finish;
    }
    json_object_object_del(groups, "fonts");
    if (json_object_to_file_ext(path, root, JSON_C_TO_STRING_PRETTY) != 0) {
        passed = false;
        goto finish;
    }
    json_object_put(root);
    root = NULL;
    error[0] = '\0';
    if (packages_load_json(&loaded, path, error, sizeof(error)) ||
        strstr(error, "fonts") == NULL) {
        (void)fprintf(stderr, "incomplete package config was not rejected: %s\n", error);
        passed = false;
    }

finish:
    /* 无论场景成功与否都释放 JSON 对象并移除临时文件。 */
    if (root != NULL) json_object_put(root);
    (void)unlink(path);
    (void)rmdir(directory);
    (void)printf("%s package configuration round trip and validation\n",
                 passed ? "PASS" : "FAIL");
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
