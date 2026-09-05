#define _POSIX_C_SOURCE 200809L

#include "packages.h"
#include "util.h"

#include <json-c/json.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

    if (mkdtemp(directory) == NULL) return EXIT_FAILURE;
    (void)snprintf(path, sizeof(path), "%s/packages.json", directory);
    packages_init_defaults(&defaults);
    if (!packages_save_json(&defaults, path, error, sizeof(error)) ||
        !packages_load_json(&loaded, path, error, sizeof(error))) {
        (void)fprintf(stderr, "package config round trip failed: %s\n", error);
        passed = false;
        goto finish;
    }
    if (loaded.groups[PKG_HYPRLAND].count != defaults.groups[PKG_HYPRLAND].count ||
        strcmp(loaded.groups[PKG_CORE].values[0],
               defaults.groups[PKG_CORE].values[0]) != 0) {
        (void)fprintf(stderr, "package config changed during round trip\n");
        passed = false;
        goto finish;
    }

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
    if (root != NULL) json_object_put(root);
    (void)unlink(path);
    (void)rmdir(directory);
    (void)printf("%s package configuration round trip and validation\n",
                 passed ? "PASS" : "FAIL");
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
