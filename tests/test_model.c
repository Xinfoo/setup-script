#define _POSIX_C_SOURCE 200809L

#include "model.h"
#include "util.h"

#include <json-c/json.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MIB (UINT64_C(1024) * UINT64_C(1024))
#define GIB (UINT64_C(1024) * MIB)
#define TIB (UINT64_C(1024) * GIB)
#define GPT_ESP_TYPE "c12a7328-f81f-11d2-ba4b-00a0c93ec93b"
#define GPT_LINUX_TYPE "0fc63daf-8483-4772-8e79-3d69d8477de4"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n",               \
                          __FILE__, __LINE__, #condition);                       \
            return false;                                                       \
        }                                                                       \
    } while (0)

typedef bool (*TestFunction)(void);

typedef struct {
    const char *name;
    TestFunction function;
} TestCase;

typedef void (*JsonMutation)(struct json_object *root);

static void make_disk(DiskInfo *disk, size_t partition_count)
{
    memset(disk, 0, sizeof(*disk));
    copy_text(disk->name, sizeof(disk->name), "nvme0n1");
    copy_text(disk->path, sizeof(disk->path), "/dev/nvme0n1");
    copy_text(disk->model, sizeof(disk->model), "Test NVMe");
    copy_text(disk->serial, sizeof(disk->serial), "TEST-SERIAL-001");
    copy_text(disk->transport, sizeof(disk->transport), "nvme");
    copy_text(disk->partition_table, sizeof(disk->partition_table), "gpt");
    disk->size_bytes = UINT64_C(2) * TIB;
    disk->partition_count = partition_count;
}

static void make_partition(PartitionInfo *partition, unsigned number,
                           uint64_t size_bytes, const char *filesystem)
{
    char path[AI_PATH_LEN];
    memset(partition, 0, sizeof(*partition));
    (void)snprintf(path, sizeof(path), "/dev/nvme0n1p%u", number);
    copy_text(partition->path, sizeof(partition->path), path);
    copy_text(partition->current_fs, sizeof(partition->current_fs), filesystem);
    (void)snprintf(partition->fs_uuid, sizeof(partition->fs_uuid),
                   "30000000-0000-0000-0000-%012u", number);
    (void)snprintf(partition->part_uuid, sizeof(partition->part_uuid),
                   "10000000-0000-0000-0000-%012u", number);
    copy_text(partition->part_type, sizeof(partition->part_type),
              number == 1 ? GPT_ESP_TYPE : GPT_LINUX_TYPE);
    partition->number = number;
    partition->size_bytes = size_bytes;
    partition->start_sector = UINT64_C(2048) + (uint64_t)(number - 1) * UINT64_C(2097152);
}

static void make_second_disk(DiskInfo *disk, size_t partition_count)
{
    make_disk(disk, partition_count);
    copy_text(disk->name, sizeof(disk->name), "sdb");
    copy_text(disk->path, sizeof(disk->path), "/dev/sdb");
    copy_text(disk->model, sizeof(disk->model), "Test data disk");
    copy_text(disk->serial, sizeof(disk->serial), "TEST-SERIAL-002");
    for (size_t index = 0; index < partition_count; ++index) {
        make_partition(&disk->partitions[index], (unsigned)index + 1,
                       index == 0 ? UINT64_C(500) * GIB : UINT64_C(250) * GIB,
                       "ext4");
        (void)snprintf(disk->partitions[index].path,
                       sizeof(disk->partitions[index].path), "/dev/sdb%zu", index + 1);
        copy_text(disk->partitions[index].part_type,
                  sizeof(disk->partitions[index].part_type), GPT_LINUX_TYPE);
    }
}

static bool report_contains(const ValidationReport *report, IssueSeverity severity,
                            const char *text)
{
    for (size_t index = 0; index < report->count; ++index) {
        if (report->issues[index].severity == severity &&
            strstr(report->issues[index].message, text) != NULL) {
            return true;
        }
    }
    return false;
}

static bool expect_mutated_json_rejected(JsonMutation mutation, const char *expected_error)
{
    DiskInfo disk;
    InstallPlan source;
    InstallPlan loaded;
    struct json_object *root;
    char path[] = "/tmp/arch-install-plan-negative-XXXXXX";
    char error[256] = {0};
    int descriptor;
    bool rejected;

    make_disk(&disk, 0);
    plan_init(&source);
    plan_select_disk(&source, &disk);
    plan_use_automatic(&source, &disk, STORAGE_AUTO_ROOT_SWAP);

    descriptor = mkstemp(path);
    if (descriptor < 0 || close(descriptor) != 0) {
        if (descriptor >= 0) (void)unlink(path);
        return false;
    }
    if (!plan_save_json(&source, path, error, sizeof(error))) {
        (void)fprintf(stderr, "plan_save_json failed: %s\n", error);
        (void)unlink(path);
        return false;
    }
    root = json_object_from_file(path);
    if (root == NULL) {
        (void)fprintf(stderr, "cannot parse temporary plan for mutation\n");
        (void)unlink(path);
        return false;
    }
    mutation(root);
    if (json_object_to_file_ext(path, root,
                                JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_SPACED) != 0) {
        (void)fprintf(stderr, "cannot write mutated temporary plan\n");
        json_object_put(root);
        (void)unlink(path);
        return false;
    }
    json_object_put(root);

    rejected = !plan_load_json(&loaded, path, error, sizeof(error));
    (void)unlink(path);
    if (!rejected || strstr(error, expected_error) == NULL) {
        (void)fprintf(stderr, "expected JSON rejection containing [%s], got [%s]\n",
                      expected_error, rejected ? error : "plan was accepted");
        return false;
    }
    return true;
}

static void mutate_boolean_to_string(struct json_object *root)
{
    struct json_object *system = NULL;

    (void)json_object_object_get_ex(root, "system", &system);
    json_object_object_add(system, "firewall", json_object_new_string("false"));
}

static void mutate_disk_size_to_negative(struct json_object *root)
{
    struct json_object *storage = NULL;
    struct json_object *disks = NULL;
    struct json_object *disk = NULL;

    (void)json_object_object_get_ex(root, "storage", &storage);
    (void)json_object_object_get_ex(storage, "disks", &disks);
    disk = json_object_array_get_idx(disks, 0);
    json_object_object_add(disk, "size_bytes", json_object_new_int64(-1));
}

static void mutate_to_incomplete_version_one(struct json_object *root)
{
    struct json_object *storage = NULL;

    json_object_object_add(root, "version", json_object_new_int(1));
    (void)json_object_object_get_ex(root, "storage", &storage);
    json_object_object_del(storage, "disks");
}

static bool test_default_plan_fails_validation(void)
{
    InstallPlan plan;
    ValidationReport report;

    plan_init(&plan);
    validate_plan(&plan, &report);

    CHECK(report.error_count > 0);
    CHECK(report_contains(&report, ISSUE_ERROR, "Add at least one installation disk"));
    CHECK(report_contains(&report, ISSUE_ERROR, "Exactly one root"));
    CHECK(report_contains(&report, ISSUE_ERROR, "Exactly one EFI"));
    return true;
}

static bool test_valid_automatic_layout(void)
{
    DiskInfo disk;
    InstallPlan plan;
    ValidationReport report;

    make_disk(&disk, 0);
    plan_init(&plan);
    plan_select_disk(&plan, &disk);
    plan_use_automatic(&plan, &disk, STORAGE_AUTO_HOME_SWAP);
    validate_plan(&plan, &report);

    CHECK(report.error_count == 0);
    CHECK(plan.storage.disks[0].mode == STORAGE_AUTO_HOME_SWAP);
    CHECK(plan.storage.disks[0].partition_count == 4);
    CHECK(strcmp(plan.storage.disks[0].partitions[0].device, "/dev/nvme0n1p1") == 0);
    CHECK(plan.storage.disks[0].partitions[0].usage == PART_BOOT);
    CHECK(plan.storage.disks[0].partitions[0].target_fs == FS_VFAT);
    CHECK(plan.storage.disks[0].partitions[1].usage == PART_ROOT);
    CHECK(plan.storage.disks[0].partitions[1].size_bytes == UINT64_C(100) * GIB);
    CHECK(plan.storage.disks[0].partitions[2].usage == PART_HOME);
    CHECK(plan.storage.disks[0].partitions[2].size_bytes > 0);
    CHECK(plan.storage.disks[0].partitions[3].usage == PART_SWAP);
    CHECK(plan.storage.disks[0].partitions[3].target_fs == FS_SWAP);
    return true;
}

static bool test_existing_partitions_keep_and_format(void)
{
    DiskInfo disk;
    InstallPlan plan;
    ValidationReport report;
    PartitionPlan *boot;
    PartitionPlan *root;
    PartitionPlan *home;

    make_disk(&disk, 3);
    make_partition(&disk.partitions[0], 1, GIB, "vfat");
    make_partition(&disk.partitions[1], 2, UINT64_C(100) * GIB, "ext4");
    make_partition(&disk.partitions[2], 3, UINT64_C(500) * GIB, "xfs");

    plan_init(&plan);
    plan_select_disk(&plan, &disk);
    boot = &plan.storage.disks[0].partitions[0];
    root = &plan.storage.disks[0].partitions[1];
    home = &plan.storage.disks[0].partitions[2];

    boot->usage = PART_BOOT;
    CHECK(boot->action == ACTION_KEEP);

    root->usage = PART_ROOT;
    CHECK(root->action == ACTION_KEEP);
    plan_cycle_format(root);
    CHECK(root->action == ACTION_FORMAT);
    CHECK(root->target_fs == FS_EXT4);
    plan_cycle_format(root);
    CHECK(root->target_fs == FS_XFS);

    home->usage = PART_HOME;
    CHECK(home->action == ACTION_KEEP);

    validate_plan(&plan, &report);
    CHECK(report.error_count == 0);
    CHECK(filesystem_from_name(boot->current_fs) == FS_VFAT);
    CHECK(filesystem_from_name(home->current_fs) == FS_XFS);
    return true;
}

static bool test_duplicate_usage_is_rejected(void)
{
    DiskInfo disk;
    InstallPlan plan;
    ValidationReport report;

    make_disk(&disk, 3);
    make_partition(&disk.partitions[0], 1, GIB, "vfat");
    make_partition(&disk.partitions[1], 2, UINT64_C(100) * GIB, "ext4");
    make_partition(&disk.partitions[2], 3, UINT64_C(100) * GIB, "ext4");
    plan_init(&plan);
    plan_select_disk(&plan, &disk);

    plan.storage.disks[0].partitions[0].usage = PART_BOOT;
    plan.storage.disks[0].partitions[1].usage = PART_ROOT;
    plan.storage.disks[0].partitions[2].usage = PART_ROOT;
    validate_plan(&plan, &report);

    CHECK(report.error_count > 0);
    CHECK(report_contains(&report, ISSUE_ERROR,
                          "Mount target / is assigned more than once"));
    return true;
}

static bool partitions_equal(const PartitionPlan *left, const PartitionPlan *right)
{
    return strcmp(left->device, right->device) == 0 &&
           strcmp(left->current_fs, right->current_fs) == 0 &&
           strcmp(left->fs_uuid, right->fs_uuid) == 0 &&
           strcmp(left->part_uuid, right->part_uuid) == 0 &&
           strcmp(left->part_type, right->part_type) == 0 &&
           left->size_bytes == right->size_bytes &&
           left->start_sector == right->start_sector &&
           left->number == right->number &&
           left->planned == right->planned &&
           left->usage == right->usage &&
           left->action == right->action &&
           left->target_fs == right->target_fs &&
           left->f2fs_mode == right->f2fs_mode;
}

static bool system_plans_equal(const SystemPlan *left, const SystemPlan *right)
{
    return left->platform == right->platform &&
           left->kernel == right->kernel &&
           left->locale == right->locale &&
           left->desktop == right->desktop &&
           strcmp(left->timezone, right->timezone) == 0 &&
           strcmp(left->hostname, right->hostname) == 0 &&
           strcmp(left->username, right->username) == 0 &&
           left->laptop == right->laptop &&
           left->intel_graphics == right->intel_graphics &&
           left->nvidia_graphics == right->nvidia_graphics &&
           left->bluetooth == right->bluetooth &&
           left->desktop_recommended == right->desktop_recommended &&
           left->chinese_input == right->chinese_input &&
           left->firewall == right->firewall &&
           left->printer == right->printer &&
           left->archive_tools == right->archive_tools &&
           left->terminal_tools == right->terminal_tools &&
           left->extra_tools == right->extra_tools &&
           left->desktop_apps == right->desktop_apps &&
           left->local_mirror == right->local_mirror &&
           left->china_mirrors == right->china_mirrors &&
           left->secure_boot == right->secure_boot &&
           left->create_efi_entry == right->create_efi_entry;
}

static bool plans_equal(const InstallPlan *left, const InstallPlan *right)
{
    if (left->version != right->version ||
        left->storage.disk_count != right->storage.disk_count ||
        !system_plans_equal(&left->system, &right->system)) {
        return false;
    }
    for (size_t disk_index = 0; disk_index < left->storage.disk_count; ++disk_index) {
        const DiskPlan *left_disk = &left->storage.disks[disk_index];
        const DiskPlan *right_disk = &right->storage.disks[disk_index];
        if (strcmp(left_disk->path, right_disk->path) != 0 ||
            strcmp(left_disk->model, right_disk->model) != 0 ||
            strcmp(left_disk->serial, right_disk->serial) != 0 ||
            strcmp(left_disk->partition_table, right_disk->partition_table) != 0 ||
            left_disk->size_bytes != right_disk->size_bytes ||
            left_disk->removable != right_disk->removable ||
            left_disk->read_only != right_disk->read_only ||
            left_disk->in_use != right_disk->in_use ||
            left_disk->mode != right_disk->mode ||
            left_disk->partition_count != right_disk->partition_count) return false;
        for (size_t index = 0; index < left_disk->partition_count; ++index) {
            if (!partitions_equal(&left_disk->partitions[index],
                                  &right_disk->partitions[index])) return false;
        }
    }
    return true;
}

static bool test_json_round_trip(void)
{
    DiskInfo disk;
    DiskInfo second_disk;
    InstallPlan source;
    InstallPlan loaded;
    char path[] = "/tmp/arch-install-plan-test-XXXXXX";
    char error[256] = {0};
    int descriptor;

    make_disk(&disk, 0);
    plan_init(&source);
    plan_select_disk(&source, &disk);
    plan_use_automatic(&source, &disk, STORAGE_AUTO_ROOT_SWAP);
    make_second_disk(&second_disk, 0);
    CHECK(plan_add_disk(&source, &second_disk));
    disk_plan_use_automatic(&source.storage.disks[1], &second_disk, STORAGE_AUTO_DATA);
    source.storage.disks[0].partitions[1].target_fs = FS_F2FS;
    source.storage.disks[0].partitions[1].f2fs_mode = F2FS_COMPRESSED;
    source.system.platform = PLATFORM_AMD;
    source.system.kernel = KERNEL_ZEN;
    source.system.locale = LOCALE_ZH_CN;
    source.system.desktop = DESKTOP_HYPRLAND;
    copy_text(source.system.timezone, sizeof(source.system.timezone), "Europe/Helsinki");
    copy_text(source.system.hostname, sizeof(source.system.hostname), "TEST-HOST");
    copy_text(source.system.username, sizeof(source.system.username), "tester");
    source.system.laptop = true;
    source.system.intel_graphics = true;
    source.system.nvidia_graphics = true;
    source.system.bluetooth = true;
    source.system.desktop_recommended = false;
    source.system.chinese_input = true;
    source.system.firewall = true;
    source.system.printer = true;
    source.system.archive_tools = false;
    source.system.terminal_tools = false;
    source.system.extra_tools = true;
    source.system.desktop_apps = true;
    source.system.local_mirror = true;
    source.system.china_mirrors = false;
    source.system.secure_boot = true;
    source.system.create_efi_entry = false;

    descriptor = mkstemp(path);
    CHECK(descriptor >= 0);
    CHECK(close(descriptor) == 0);
    if (!plan_save_json(&source, path, error, sizeof(error))) {
        (void)fprintf(stderr, "plan_save_json failed: %s\n", error);
        (void)unlink(path);
        return false;
    }
    if (!plan_load_json(&loaded, path, error, sizeof(error))) {
        (void)fprintf(stderr, "plan_load_json failed: %s\n", error);
        (void)unlink(path);
        return false;
    }
    CHECK(unlink(path) == 0);
    CHECK(plans_equal(&source, &loaded));
    return true;
}

static bool test_multi_disk_mounts_and_format_only(void)
{
    DiskInfo system_disk;
    DiskInfo data_disk;
    InstallPlan plan;
    ValidationReport report;

    make_disk(&system_disk, 2);
    make_partition(&system_disk.partitions[0], 1, GIB, "vfat");
    make_partition(&system_disk.partitions[1], 2, UINT64_C(100) * GIB, "ext4");
    make_second_disk(&data_disk, 2);
    plan_init(&plan);
    plan_select_disk(&plan, &system_disk);
    CHECK(plan_add_disk(&plan, &data_disk));
    plan.storage.disks[0].partitions[0].usage = PART_BOOT;
    plan.storage.disks[0].partitions[1].usage = PART_ROOT;
    plan.storage.disks[0].partitions[1].action = ACTION_FORMAT;
    plan.storage.disks[0].partitions[1].target_fs = FS_EXT4;
    plan.storage.disks[1].partitions[0].usage = PART_HOME;
    plan.storage.disks[1].partitions[1].usage = PART_UNUSED;
    plan.storage.disks[1].partitions[1].action = ACTION_FORMAT;
    plan.storage.disks[1].partitions[1].target_fs = FS_XFS;

    validate_plan(&plan, &report);
    CHECK(report.error_count == 0);
    return true;
}

static bool test_root_and_boot_must_share_disk(void)
{
    DiskInfo system_disk;
    DiskInfo data_disk;
    InstallPlan plan;
    ValidationReport report;

    make_disk(&system_disk, 1);
    make_partition(&system_disk.partitions[0], 1, GIB, "vfat");
    make_second_disk(&data_disk, 1);
    plan_init(&plan);
    plan_select_disk(&plan, &system_disk);
    CHECK(plan_add_disk(&plan, &data_disk));
    plan.storage.disks[0].partitions[0].usage = PART_BOOT;
    plan.storage.disks[1].partitions[0].usage = PART_ROOT;
    plan.storage.disks[1].partitions[0].action = ACTION_FORMAT;
    plan.storage.disks[1].partitions[0].target_fs = FS_EXT4;

    validate_plan(&plan, &report);
    CHECK(report_contains(&report, ISSUE_ERROR,
                          "root (/) and EFI (/boot) partitions must be on the same disk"));
    return true;
}

static bool test_json_boolean_string_is_rejected(void)
{
    CHECK(expect_mutated_json_rejected(mutate_boolean_to_string,
                                       "invalid or missing JSON field system.firewall"));
    return true;
}

static bool test_json_negative_size_is_rejected(void)
{
    CHECK(expect_mutated_json_rejected(mutate_disk_size_to_negative,
                                       "storage.disks[0].size_bytes must be a non-negative integer"));
    return true;
}

static bool test_old_version_is_reported_before_schema_errors(void)
{
    CHECK(expect_mutated_json_rejected(mutate_to_incomplete_version_one,
                                       "unsupported plan version: 1"));
    return true;
}

static bool test_unsafe_device_paths_are_rejected(void)
{
    DiskInfo disk;
    InstallPlan plan;
    ValidationReport report;

    make_disk(&disk, 0);
    plan_init(&plan);
    plan_select_disk(&plan, &disk);
    plan_use_automatic(&plan, &disk, STORAGE_AUTO_ROOT_SWAP);
    copy_text(plan.storage.disks[0].path, sizeof(plan.storage.disks[0].path),
              "/dev/nvme0n1\nSFDISK");
    validate_plan(&plan, &report);
    CHECK(report_contains(&report, ISSUE_ERROR, "not a supported /dev device path"));

    plan_select_disk(&plan, &disk);
    plan_use_automatic(&plan, &disk, STORAGE_AUTO_ROOT_SWAP);
    copy_text(plan.storage.disks[0].partitions[0].device,
              sizeof(plan.storage.disks[0].partitions[0].device),
              "/dev/nvme0n1p1\nSFDISK");
    validate_plan(&plan, &report);
    CHECK(report_contains(&report, ISSUE_ERROR,
                          "partition does not belong to its installation disk"));
    return true;
}

static bool test_tampered_automatic_sizes_are_rejected(void)
{
    DiskInfo disk;
    InstallPlan plan;
    ValidationReport report;

    make_disk(&disk, 0);
    plan_init(&plan);
    plan_select_disk(&plan, &disk);
    plan_use_automatic(&plan, &disk, STORAGE_AUTO_ROOT_SWAP);
    plan.storage.disks[0].partitions[2].size_bytes = 1;
    validate_plan(&plan, &report);
    CHECK(report_contains(&report, ISSUE_ERROR,
                          "automatic partition must be at least 1 MiB"));

    plan_use_automatic(&plan, &disk, STORAGE_AUTO_ROOT_SWAP);
    plan.storage.disks[0].partitions[1].size_bytes -= GIB;
    validate_plan(&plan, &report);
    CHECK(report_contains(&report, ISSUE_ERROR,
                          "must account for the entire installation disk"));

    plan_use_automatic(&plan, &disk, STORAGE_AUTO_HOME_SWAP);
    plan.storage.disks[0].partitions[2].size_bytes = MIB;
    validate_plan(&plan, &report);
    CHECK(report_contains(&report, ISSUE_ERROR,
                          "guided home partition must be at least 8 GiB"));
    return true;
}

static bool test_var_and_usr_keep_are_rejected(void)
{
    DiskInfo disk;
    InstallPlan plan;
    ValidationReport report;

    make_disk(&disk, 3);
    make_partition(&disk.partitions[0], 1, GIB, "vfat");
    make_partition(&disk.partitions[1], 2, UINT64_C(100) * GIB, "ext4");
    make_partition(&disk.partitions[2], 3, UINT64_C(100) * GIB, "ext4");
    plan_init(&plan);
    plan_select_disk(&plan, &disk);
    plan.storage.disks[0].partitions[0].usage = PART_BOOT;
    plan.storage.disks[0].partitions[1].usage = PART_ROOT;
    plan.storage.disks[0].partitions[1].action = ACTION_FORMAT;
    plan.storage.disks[0].partitions[1].target_fs = FS_EXT4;
    plan.storage.disks[0].partitions[2].usage = PART_VAR;

    validate_plan(&plan, &report);
    CHECK(report_contains(&report, ISSUE_ERROR,
                          "Root, /var, and /usr partitions must be formatted"));

    plan.storage.disks[0].partitions[2].usage = PART_USR;
    validate_plan(&plan, &report);
    CHECK(report_contains(&report, ISSUE_ERROR,
                          "Root, /var, and /usr partitions must be formatted"));
    return true;
}

static bool check_quote(const char *input, const char *expected)
{
    char *quoted = shell_quote(input);
    bool equal = quoted != NULL && strcmp(quoted, expected) == 0;
    if (!equal) {
        (void)fprintf(stderr, "shell_quote(%s): expected [%s], got [%s]\n",
                      input != NULL ? input : "NULL", expected,
                      quoted != NULL ? quoted : "NULL");
    }
    free(quoted);
    return equal;
}

static bool test_shell_quote(void)
{
    CHECK(check_quote(NULL, "''"));
    CHECK(check_quote("", "''"));
    CHECK(check_quote("plain", "'plain'"));
    CHECK(check_quote("two words", "'two words'"));
    CHECK(check_quote("a'b", "'a'\\''b'"));
    CHECK(check_quote("$(echo unsafe); $HOME", "'$(echo unsafe); $HOME'"));
    return true;
}

static bool test_tampered_automatic_layout_is_rejected(void)
{
    DiskInfo disk;
    InstallPlan plan;
    ValidationReport report;

    make_disk(&disk, 0);
    plan_init(&plan);
    plan_select_disk(&plan, &disk);
    plan_use_automatic(&plan, &disk, STORAGE_AUTO_HOME_SWAP);
    plan.storage.disks[0].partition_count = 3;
    validate_plan(&plan, &report);
    CHECK(report.error_count > 0);
    CHECK(report_contains(&report, ISSUE_ERROR, "fixed partition schema"));

    plan_use_automatic(&plan, &disk, STORAGE_AUTO_ROOT_SWAP);
    plan.storage.disks[0].partitions[0].number = 2;
    plan.storage.disks[0].partitions[1].number = 1;
    validate_plan(&plan, &report);
    CHECK(report.error_count > 0);
    CHECK(report_contains(&report, ISSUE_ERROR, "fixed partition schema"));
    return true;
}

static bool test_reserved_username_is_rejected(void)
{
    CHECK(!valid_username("root"));
    CHECK(!valid_username("nobody"));
    CHECK(valid_username("arch-user"));
    return true;
}

static bool test_kept_f2fs_requires_compatibility_profile(void)
{
    DiskInfo disk;
    InstallPlan plan;
    ValidationReport report;

    make_disk(&disk, 3);
    make_partition(&disk.partitions[0], 1, GIB, "vfat");
    make_partition(&disk.partitions[1], 2, UINT64_C(100) * GIB, "ext4");
    make_partition(&disk.partitions[2], 3, UINT64_C(500) * GIB, "f2fs");
    plan_init(&plan);
    plan_select_disk(&plan, &disk);
    plan.storage.disks[0].partitions[0].usage = PART_BOOT;
    plan.storage.disks[0].partitions[1].usage = PART_ROOT;
    plan.storage.disks[0].partitions[1].action = ACTION_FORMAT;
    plan.storage.disks[0].partitions[1].target_fs = FS_EXT4;
    plan.storage.disks[0].partitions[2].usage = PART_HOME;
    plan.storage.disks[0].partitions[2].f2fs_mode = F2FS_BALANCED;
    validate_plan(&plan, &report);
    CHECK(report_contains(&report, ISSUE_ERROR,
                          "kept F2FS partition must use the compatibility mount profile"));
    return true;
}

static bool test_kept_filesystem_requires_uuid(void)
{
    DiskInfo disk;
    InstallPlan plan;
    ValidationReport report;

    make_disk(&disk, 3);
    make_partition(&disk.partitions[0], 1, GIB, "vfat");
    make_partition(&disk.partitions[1], 2, UINT64_C(100) * GIB, "ext4");
    make_partition(&disk.partitions[2], 3, UINT64_C(500) * GIB, "xfs");
    plan_init(&plan);
    plan_select_disk(&plan, &disk);
    plan.storage.disks[0].partitions[0].usage = PART_BOOT;
    plan.storage.disks[0].partitions[1].usage = PART_ROOT;
    plan.storage.disks[0].partitions[1].action = ACTION_FORMAT;
    plan.storage.disks[0].partitions[1].target_fs = FS_EXT4;
    plan.storage.disks[0].partitions[2].usage = PART_HOME;
    plan.storage.disks[0].partitions[2].fs_uuid[0] = '\0';
    validate_plan(&plan, &report);
    CHECK(report_contains(&report, ISSUE_ERROR,
                          "kept filesystem is missing its UUID identity"));
    return true;
}

static bool test_direct_partition_count_overflow_is_rejected(void)
{
    InstallPlan plan;
    ValidationReport report;

    plan_init(&plan);
    plan.storage.disk_count = 1;
    plan.storage.disks[0].partition_count = AI_MAX_PARTITIONS + 1;
    validate_plan(&plan, &report);
    CHECK(report_contains(&report, ISSUE_ERROR,
                          "partition plan exceeds the supported limit"));
    return true;
}

int main(void)
{
    static const TestCase tests[] = {
        {"default plan fails validation", test_default_plan_fails_validation},
        {"valid automatic layout", test_valid_automatic_layout},
        {"existing partitions keep and format", test_existing_partitions_keep_and_format},
        {"duplicate usage is rejected", test_duplicate_usage_is_rejected},
        {"JSON round trip", test_json_round_trip},
        {"multi-disk mounts and format-only partitions",
         test_multi_disk_mounts_and_format_only},
        {"root and boot must share one disk", test_root_and_boot_must_share_disk},
        {"JSON boolean strings are rejected", test_json_boolean_string_is_rejected},
        {"JSON negative sizes are rejected", test_json_negative_size_is_rejected},
        {"old versions are reported before schema errors",
         test_old_version_is_reported_before_schema_errors},
        {"unsafe device paths are rejected", test_unsafe_device_paths_are_rejected},
        {"tampered automatic sizes are rejected", test_tampered_automatic_sizes_are_rejected},
        {"KEEP is rejected for /var and /usr", test_var_and_usr_keep_are_rejected},
        {"shell quoting", test_shell_quote},
        {"tampered automatic layout is rejected", test_tampered_automatic_layout_is_rejected},
        {"reserved username is rejected", test_reserved_username_is_rejected},
        {"kept F2FS requires the compatibility profile",
         test_kept_f2fs_requires_compatibility_profile},
        {"kept filesystems require a UUID", test_kept_filesystem_requires_uuid},
        {"direct partition-count overflow is rejected",
         test_direct_partition_count_overflow_is_rejected},
    };
    size_t failed = 0;

    for (size_t index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        bool passed = tests[index].function();
        (void)printf("%s %s\n", passed ? "PASS" : "FAIL", tests[index].name);
        if (!passed) ++failed;
    }
    if (failed != 0) {
        (void)fprintf(stderr, "%zu test(s) failed\n", failed);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
