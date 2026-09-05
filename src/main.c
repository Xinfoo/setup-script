#define _POSIX_C_SOURCE 200809L

#include "detect.h"
#include "generator.h"
#include "model.h"
#include "ui.h"
#include "util.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool canonical_destination(const char *path, char *output, size_t output_size)
{
    char copy[PATH_MAX];
    char directory[PATH_MAX];
    char resolved[PATH_MAX];
    char *slash;
    const char *base;
    int written;

    if (strlen(path) >= sizeof(copy)) return false;
    copy_text(copy, sizeof(copy), path);
    slash = strrchr(copy, '/');
    if (slash == NULL) {
        copy_text(directory, sizeof(directory), ".");
        base = copy;
    } else {
        *slash = '\0';
        copy_text(directory, sizeof(directory), copy[0] != '\0' ? copy : "/");
        base = slash + 1;
    }
    if (base[0] == '\0' || realpath(directory, resolved) == NULL) return false;
    written = snprintf(output, output_size, "%s/%s", resolved, base);
    return written >= 0 && (size_t)written < output_size;
}

static bool paths_refer_to_same_destination(const char *left, const char *right)
{
    struct stat left_status;
    struct stat right_status;
    char left_path[PATH_MAX];
    char right_path[PATH_MAX];

    if (strcmp(left, right) == 0) return true;
    if (stat(left, &left_status) == 0 && stat(right, &right_status) == 0 &&
        left_status.st_dev == right_status.st_dev && left_status.st_ino == right_status.st_ino) {
        return true;
    }
    return canonical_destination(left, left_path, sizeof(left_path)) &&
           canonical_destination(right, right_path, sizeof(right_path)) &&
           strcmp(left_path, right_path) == 0;
}

static void usage(FILE *stream, const char *program)
{
    (void)fprintf(stream,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Build an auditable Arch Linux installation script in a TTY interface.\n"
        "\n"
        "  -p, --plan FILE       plan file to load/save (default: install-plan.json)\n"
        "  -o, --output FILE     generated script (default: install.sh)\n"
        "  -g, --generate FILE   generate from FILE without starting the TUI\n"
        "  -h, --help            show this help\n"
        "  -V, --version         show version\n",
        program);
}

int main(int argc, char **argv)
{
    const char *plan_path = "install-plan.json";
    const char *script_path = "install.sh";
    const char *generate_path = NULL;
    InstallPlan plan;
    HardwareInventory *inventory;
    ValidationReport report;
    char error[512] = {0};

    for (int index = 1; index < argc; ++index) {
        if ((strcmp(argv[index], "-p") == 0 || strcmp(argv[index], "--plan") == 0) &&
            index + 1 < argc) {
            plan_path = argv[++index];
        } else if ((strcmp(argv[index], "-o") == 0 || strcmp(argv[index], "--output") == 0) &&
                   index + 1 < argc) {
            script_path = argv[++index];
        } else if ((strcmp(argv[index], "-g") == 0 || strcmp(argv[index], "--generate") == 0) &&
                   index + 1 < argc) {
            generate_path = argv[++index];
        } else if (strcmp(argv[index], "-h") == 0 || strcmp(argv[index], "--help") == 0) {
            usage(stdout, argv[0]);
            return EXIT_SUCCESS;
        } else if (strcmp(argv[index], "-V") == 0 || strcmp(argv[index], "--version") == 0) {
            (void)puts("arch-install-builder 0.1.0");
            return EXIT_SUCCESS;
        } else {
            (void)fprintf(stderr, "Unknown or incomplete option: %s\n", argv[index]);
            usage(stderr, argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (paths_refer_to_same_destination(generate_path != NULL ? generate_path : plan_path,
                                        script_path)) {
        (void)fprintf(stderr, "Plan input and script output must use different files.\n");
        return EXIT_FAILURE;
    }

    if (generate_path != NULL) {
        if (!plan_load_json(&plan, generate_path, error, sizeof(error))) {
            (void)fprintf(stderr, "Error: %s\n", error);
            return EXIT_FAILURE;
        }
        validate_plan(&plan, &report);
        if (report.error_count != 0) {
            (void)fprintf(stderr, "Plan has %zu error(s):\n", report.error_count);
            for (size_t index = 0; index < report.count; ++index) {
                if (report.issues[index].severity == ISSUE_ERROR)
                    (void)fprintf(stderr, "  - %s\n", report.issues[index].message);
            }
            return EXIT_FAILURE;
        }
        if (!generate_install_script(&plan, script_path, error, sizeof(error))) {
            (void)fprintf(stderr, "Error: %s\n", error);
            return EXIT_FAILURE;
        }
        (void)printf("Generated %s\n", script_path);
        return EXIT_SUCCESS;
    }

    plan_init(&plan);
    if (access(plan_path, F_OK) == 0 &&
        !plan_load_json(&plan, plan_path, error, sizeof(error))) {
        (void)fprintf(stderr, "Cannot load %s: %s\n", plan_path, error);
        return EXIT_FAILURE;
    }
    inventory = calloc(1, sizeof(*inventory));
    if (inventory == NULL) {
        (void)fprintf(stderr, "Out of memory.\n");
        return EXIT_FAILURE;
    }
    if (!detect_hardware(inventory, error, sizeof(error))) {
        (void)fprintf(stderr, "Hardware detection failed: %s\n", error);
        free(inventory);
        return EXIT_FAILURE;
    }
    {
        int result = run_tui(&plan, inventory, plan_path, script_path);
        free(inventory);
        return result;
    }
}
