#define _POSIX_C_SOURCE 200809L

#include "detect.h"
#include "generator.h"
#include "model.h"
#include "packages.h"
#include "ui.h"
#include "util.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONFIG_DIRECTORY "config"
#define PACKAGE_CONFIG_PATH CONFIG_DIRECTORY "/packages.json"

/* 软件包配置损坏或版本不匹配时，仅在交互式终端中允许用户确认覆盖。 */
static bool confirm_package_config_overwrite(void)
{
    char answer[32];

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return false;
    (void)fputs("Overwrite config/packages.json with the built-in defaults? [y/N] ", stdout);
    (void)fflush(stdout);
    if (fgets(answer, sizeof(answer), stdin) == NULL) return false;
    return answer[0] == 'y' || answer[0] == 'Y';
}

static bool prepare_package_config(PackageConfig *packages,
                                   char *error, size_t error_size)
{
    struct stat status;

    /* 首次运行时创建配置目录，但拒绝把同名普通文件或链接当作目录使用。 */
    if (lstat(CONFIG_DIRECTORY, &status) != 0) {
        if (errno != ENOENT) {
            (void)snprintf(error, error_size, "cannot inspect %s: %s",
                           CONFIG_DIRECTORY, strerror(errno));
            return false;
        }
        if (mkdir(CONFIG_DIRECTORY, 0755) != 0) {
            (void)snprintf(error, error_size, "cannot create %s: %s",
                           CONFIG_DIRECTORY, strerror(errno));
            return false;
        }
    } else if (!S_ISDIR(status.st_mode)) {
        (void)snprintf(error, error_size, "%s is not a real directory",
                       CONFIG_DIRECTORY);
        return false;
    }

    /* 缺失时写入内建默认值；已存在时必须完整通过当前版本的严格校验。 */
    if (lstat(PACKAGE_CONFIG_PATH, &status) != 0) {
        if (errno != ENOENT) {
            (void)snprintf(error, error_size, "cannot inspect %s: %s",
                           PACKAGE_CONFIG_PATH, strerror(errno));
            return false;
        }
        packages_init_defaults(packages);
        return packages_save_json(packages, PACKAGE_CONFIG_PATH, error, error_size);
    }
    if (packages_load_json(packages, PACKAGE_CONFIG_PATH, error, error_size)) return true;

    (void)fprintf(stderr, "Package configuration error: %s\n", error);
    if (!confirm_package_config_overwrite()) {
        (void)snprintf(error, error_size,
                       "package configuration was not overwritten");
        return false;
    }
    packages_init_defaults(packages);
    return packages_save_json(packages, PACKAGE_CONFIG_PATH, error, error_size);
}

static bool canonical_destination(const char *path, char *output, size_t output_size)
{
    char copy[PATH_MAX];
    char directory[PATH_MAX];
    char resolved[PATH_MAX];
    char *slash;
    const char *base;
    int written;

    /*
     * 输出文件可能尚不存在，所以只 realpath 父目录，再把末级名称接回去；
     * 这样仍可识别 a/../b、相对路径等指向同一待创建文件的写法。
     */
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

/* 同时比较文本路径、现有文件身份和父目录规范路径，避免输入覆盖生成脚本。 */
static bool paths_refer_to_same_destination(const char *left, const char *right)
{
    struct stat left_status;
    struct stat right_status;
    char left_path[PATH_MAX];
    char right_path[PATH_MAX];

    /* 由便宜到昂贵依次检查字面相等、现有 inode 相等和规范化后的目标相等。 */
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
        "  -p, --plan FILE       plan file to load/save (default: config/install-plan.json)\n"
        "  -o, --output FILE     generated script (default: install.sh)\n"
        "  -g, --generate FILE   generate from FILE without starting the TUI\n"
        "  -h, --help            show this help\n"
        "  -V, --version         show version\n",
        program);
}

int main(int argc, char **argv)
{
    const char *plan_path = "config/install-plan.json";
    const char *script_path = "install.sh";
    const char *generate_path = NULL;
    InstallPlan plan;
    PackageConfig packages;
    HardwareInventory *inventory;
    ValidationReport report;
    char error[512] = {0};

    /* 命令行只负责选择计划路径、输出路径以及是否跳过 TUI 直接生成。 */
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

    /* 两种运行模式共用同一份外部软件包配置。 */
    if (!prepare_package_config(&packages, error, sizeof(error))) {
        (void)fprintf(stderr, "Error: %s\n", error);
        return EXIT_FAILURE;
    }

    /* 防止原子替换输出脚本时把作为输入或稍后保存的计划文件覆盖掉。 */
    if (paths_refer_to_same_destination(generate_path != NULL ? generate_path : plan_path,
                                        script_path)) {
        (void)fprintf(stderr, "Plan input and script output must use different files.\n");
        return EXIT_FAILURE;
    }

    /* 非交互模式加载并验证现有计划，验证通过后直接生成安装脚本。 */
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
        if (!generate_install_script(&plan, &packages, script_path, error, sizeof(error))) {
            (void)fprintf(stderr, "Error: %s\n", error);
            return EXIT_FAILURE;
        }
        (void)printf("Generated %s\n", script_path);
        return EXIT_SUCCESS;
    }

    /* 交互模式可从已有计划继续编辑，再结合当前硬件清单启动 TUI。 */
    plan_init(&plan);
    if (access(plan_path, F_OK) == 0) {
        if (!plan_load_json(&plan, plan_path, error, sizeof(error))) {
            (void)fprintf(stderr, "Cannot load %s: %s\n", plan_path, error);
            return EXIT_FAILURE;
        }
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
        int result = run_tui(&plan, inventory, &packages, plan_path, script_path);
        free(inventory);
        return result;
    }
}
