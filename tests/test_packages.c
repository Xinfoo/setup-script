#define _POSIX_C_SOURCE 200809L

#include "packages.h"
#include "util.h"

#include <json-c/json.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    bool passed = true;

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
