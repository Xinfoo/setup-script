#define _POSIX_C_SOURCE 200809L

#include "model.h"

#include "util.h"

#include <json-c/json.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define MIB (UINT64_C(1024) * UINT64_C(1024))
#define GIB (UINT64_C(1024) * UINT64_C(1024) * UINT64_C(1024))
#define GPT_ESP_TYPE "c12a7328-f81f-11d2-ba4b-00a0c93ec93b"

static bool valid_block_device_path(const char *value)
{
    const unsigned char *cursor;

    if (value == NULL || strncmp(value, "/dev/", 5) != 0 || value[5] == '\0') {
        return false;
    }
    for (cursor = (const unsigned char *)value + 5; *cursor != '\0'; ++cursor) {
        if (!isalnum(*cursor) && *cursor != '_' && *cursor != '-' && *cursor != '.') {
            return false;
        }
    }
    return true;
}

static void partition_device(char *output, size_t size, const char *disk, unsigned number)
{
    char suffix[32];
    size_t disk_length;
    size_t output_length;
    bool needs_p;

    if (size == 0) return;
    disk_length = strlen(disk);
    needs_p = disk_length > 0 && disk[disk_length - 1] >= '0' && disk[disk_length - 1] <= '9';
    (void)snprintf(suffix, sizeof(suffix), "%s%u", needs_p ? "p" : "", number);
    output_length = disk_length < size - 1 ? disk_length : size - 1;
    memmove(output, disk, output_length);
    output[output_length] = '\0';
    copy_text(output + output_length, size - output_length, suffix);
}

void plan_init(InstallPlan *plan)
{
    memset(plan, 0, sizeof(*plan));
    plan->version = 3;
    plan->system.platform = PLATFORM_INTEL;
    plan->system.kernel = KERNEL_LINUX;
    plan->system.locale = LOCALE_EN_US;
    plan->system.desktop = DESKTOP_KDE;
    copy_text(plan->system.timezone, sizeof(plan->system.timezone), "Asia/Shanghai");
    copy_text(plan->system.hostname, sizeof(plan->system.hostname), "ARCH-LINUX");
    copy_text(plan->system.username, sizeof(plan->system.username), "user");
    plan->system.desktop_recommended = true;
    plan->system.archive_tools = true;
    plan->system.terminal_tools = true;
    plan->system.china_mirrors = true;
    plan->system.create_efi_entry = true;
}

void plan_select_disk(InstallPlan *plan, const DiskInfo *disk)
{
    memset(&plan->storage, 0, sizeof(plan->storage));
    (void)plan_add_disk(plan, disk);
}

DiskPlan *plan_find_disk(InstallPlan *plan, const char *path)
{
    for (size_t index = 0; index < plan->storage.disk_count; ++index) {
        if (strcmp(plan->storage.disks[index].path, path) == 0) {
            return &plan->storage.disks[index];
        }
    }
    return NULL;
}

void disk_plan_use_existing(DiskPlan *plan, const DiskInfo *disk)
{
    plan->mode = STORAGE_EXISTING;
    plan->partition_count = disk->partition_count;
    if (plan->partition_count > AI_MAX_PARTITIONS) {
        plan->partition_count = AI_MAX_PARTITIONS;
    }
    for (size_t index = 0; index < plan->partition_count; ++index) {
        const PartitionInfo *source = &disk->partitions[index];
        PartitionPlan *target = &plan->partitions[index];
        memset(target, 0, sizeof(*target));
        copy_text(target->device, sizeof(target->device), source->path);
        copy_text(target->current_fs, sizeof(target->current_fs), source->current_fs);
        copy_text(target->fs_uuid, sizeof(target->fs_uuid), source->fs_uuid);
        copy_text(target->part_uuid, sizeof(target->part_uuid), source->part_uuid);
        copy_text(target->part_type, sizeof(target->part_type), source->part_type);
        target->size_bytes = source->size_bytes;
        target->start_sector = source->start_sector;
        target->number = source->number;
        target->usage = PART_UNUSED;
        target->action = ACTION_KEEP;
        target->target_fs = FS_NONE;
        target->f2fs_mode = F2FS_DEFAULT;
    }
}

bool plan_add_disk(InstallPlan *plan, const DiskInfo *disk)
{
    DiskPlan *target;

    if (plan_find_disk(plan, disk->path) != NULL) return true;
    if (plan->storage.disk_count >= AI_MAX_PLAN_DISKS) return false;
    target = &plan->storage.disks[plan->storage.disk_count++];
    memset(target, 0, sizeof(*target));
    copy_text(target->path, sizeof(target->path), disk->path);
    copy_text(target->model, sizeof(target->model), disk->model);
    copy_text(target->serial, sizeof(target->serial), disk->serial);
    copy_text(target->partition_table, sizeof(target->partition_table), disk->partition_table);
    target->size_bytes = disk->size_bytes;
    target->removable = disk->removable;
    target->read_only = disk->read_only;
    target->in_use = disk->in_use;
    disk_plan_use_existing(target, disk);
    return true;
}

void plan_use_existing(InstallPlan *plan, const DiskInfo *disk)
{
    DiskPlan *target = plan_find_disk(plan, disk->path);
    if (target == NULL && plan_add_disk(plan, disk)) target = plan_find_disk(plan, disk->path);
    if (target != NULL) disk_plan_use_existing(target, disk);
}

uint64_t recommended_swap_bytes(void)
{
    FILE *file = fopen("/proc/meminfo", "r");
    unsigned long long kib = 0;
    uint64_t memory;
    if (file != NULL) {
        if (fscanf(file, "MemTotal: %llu kB", &kib) != 1) {
            kib = 0;
        }
        (void)fclose(file);
    }
    memory = (uint64_t)kib * UINT64_C(1024);
    if (memory == 0) {
        return UINT64_C(8) * GIB;
    }
    if (memory <= UINT64_C(8) * GIB) {
        return memory * 2;
    }
    if (memory <= UINT64_C(64) * GIB) {
        return memory;
    }
    return UINT64_C(8) * GIB;
}

static void auto_partition(DiskPlan *storage, size_t index, unsigned number,
                           uint64_t bytes, PartitionUsage usage, Filesystem filesystem)
{
    PartitionPlan *partition = &storage->partitions[index];
    memset(partition, 0, sizeof(*partition));
    partition_device(partition->device, sizeof(partition->device), storage->path, number);
    partition->number = number;
    partition->size_bytes = bytes;
    partition->planned = true;
    partition->usage = usage;
    partition->action = ACTION_FORMAT;
    partition->target_fs = filesystem;
    partition->f2fs_mode = F2FS_BALANCED;
}

void disk_plan_use_automatic(DiskPlan *storage, const DiskInfo *disk, StorageMode mode)
{
    uint64_t efi = GIB;
    uint64_t swap = recommended_swap_bytes();
    uint64_t remaining = disk->size_bytes > efi ? disk->size_bytes - efi : 0;

    storage->mode = mode;
    storage->partition_count = 0;
    if (mode == STORAGE_AUTO_DATA) {
        auto_partition(storage, storage->partition_count++, 1, disk->size_bytes,
                       PART_UNUSED, FS_EXT4);
        return;
    }

    auto_partition(storage, storage->partition_count++, 1, efi, PART_BOOT, FS_VFAT);

    if (mode == STORAGE_AUTO_ROOT_ONLY) {
        auto_partition(storage, storage->partition_count++, 2, remaining, PART_ROOT, FS_EXT4);
        return;
    }
    if (remaining > swap) {
        remaining -= swap;
    } else {
        remaining = 0;
    }
    if (mode == STORAGE_AUTO_HOME_SWAP) {
        uint64_t root = UINT64_C(100) * GIB;
        uint64_t home = remaining > root ? remaining - root : 0;
        auto_partition(storage, storage->partition_count++, 2, root, PART_ROOT, FS_EXT4);
        auto_partition(storage, storage->partition_count++, 3, home, PART_HOME, FS_EXT4);
        auto_partition(storage, storage->partition_count++, 4, swap, PART_SWAP, FS_SWAP);
    } else {
        auto_partition(storage, storage->partition_count++, 2, remaining, PART_ROOT, FS_EXT4);
        auto_partition(storage, storage->partition_count++, 3, swap, PART_SWAP, FS_SWAP);
    }
}

void plan_use_automatic(InstallPlan *plan, const DiskInfo *disk, StorageMode mode)
{
    DiskPlan *target = plan_find_disk(plan, disk->path);
    if (target == NULL && plan_add_disk(plan, disk)) target = plan_find_disk(plan, disk->path);
    if (target != NULL) disk_plan_use_automatic(target, disk, mode);
}

const char *filesystem_name(Filesystem value)
{
    static const char *const names[] = {"none", "vfat", "ext4", "xfs", "f2fs", "swap"};
    return value >= FS_NONE && value <= FS_SWAP ? names[value] : "unknown";
}

const char *usage_name(PartitionUsage value)
{
    static const char *const names[] = {
        "unused", "root", "boot", "home", "var", "usr", "opt", "swap"
    };
    return value >= PART_UNUSED && value <= PART_SWAP ? names[value] : "unknown";
}

const char *partition_mountpoint(PartitionUsage value)
{
    static const char *const names[] = {"-", "/", "/boot", "/home", "/var", "/usr", "/opt", "[swap]"};
    return value >= PART_UNUSED && value <= PART_SWAP ? names[value] : "-";
}

const char *action_name(PartitionAction value)
{
    return value == ACTION_FORMAT ? "format" : "keep";
}

const char *storage_mode_name(StorageMode value)
{
    static const char *const names[] = {
        "Use existing partitions", "Automatic: root + swap",
        "Automatic: root + home + swap", "Automatic: root only",
        "Automatic: single data partition"
    };
    return value >= STORAGE_EXISTING && value <= STORAGE_AUTO_DATA ? names[value] : "unknown";
}

const char *platform_name(Platform value)
{
    static const char *const names[] = {"Intel", "AMD", "Virtual machine"};
    return value >= PLATFORM_INTEL && value <= PLATFORM_VM ? names[value] : "unknown";
}

const char *kernel_name(Kernel value)
{
    static const char *const names[] = {"linux", "linux-lts", "linux-zen", "linux-hardened"};
    return value >= KERNEL_LINUX && value <= KERNEL_HARDENED ? names[value] : "unknown";
}

const char *desktop_name(Desktop value)
{
    static const char *const names[] = {"KDE Plasma", "GNOME", "Hyprland (experimental)", "None"};
    return value >= DESKTOP_KDE && value <= DESKTOP_NONE ? names[value] : "unknown";
}

const char *locale_name(LocaleChoice value)
{
    return value == LOCALE_ZH_CN ? "zh_CN.UTF-8" : "en_US.UTF-8";
}

const char *f2fs_mode_name(F2fsMountMode value)
{
    static const char *const names[] = {"default", "balanced", "compressed"};
    return value >= F2FS_DEFAULT && value <= F2FS_COMPRESSED ? names[value] : "default";
}

Filesystem filesystem_from_name(const char *name)
{
    if (name == NULL || name[0] == '\0') return FS_NONE;
    if (strcmp(name, "vfat") == 0 || strcmp(name, "fat32") == 0) return FS_VFAT;
    if (strcmp(name, "ext4") == 0) return FS_EXT4;
    if (strcmp(name, "xfs") == 0) return FS_XFS;
    if (strcmp(name, "f2fs") == 0) return FS_F2FS;
    if (strcmp(name, "swap") == 0) return FS_SWAP;
    return FS_NONE;
}

void plan_cycle_usage(PartitionPlan *partition)
{
    partition->usage = (PartitionUsage)(((int)partition->usage + 1) % ((int)PART_SWAP + 1));
    if (partition->planned && partition->usage != PART_UNUSED) {
        partition->action = ACTION_FORMAT;
    }
    if (partition->usage == PART_SWAP) {
        partition->action = ACTION_FORMAT;
        partition->target_fs = FS_SWAP;
    } else if (partition->usage == PART_BOOT && partition->action == ACTION_FORMAT) {
        partition->target_fs = FS_VFAT;
    } else if (partition->usage == PART_UNUSED) {
        partition->action = partition->planned ? ACTION_FORMAT : ACTION_KEEP;
        if (partition->planned) {
            if (partition->target_fs == FS_NONE) partition->target_fs = FS_EXT4;
        } else {
            partition->target_fs = FS_NONE;
        }
    } else if (partition->action == ACTION_FORMAT &&
               (partition->target_fs == FS_NONE || partition->target_fs == FS_SWAP ||
                partition->target_fs == FS_VFAT)) {
        partition->target_fs = FS_EXT4;
    }
}

void plan_cycle_format(PartitionPlan *partition)
{
    if (partition->usage == PART_UNUSED) {
        if (partition->action == ACTION_KEEP) {
            partition->action = ACTION_FORMAT;
            partition->target_fs = FS_EXT4;
            return;
        }
        switch (partition->target_fs) {
        case FS_EXT4: partition->target_fs = FS_XFS; break;
        case FS_XFS: partition->target_fs = FS_F2FS; break;
        case FS_F2FS: partition->target_fs = FS_VFAT; break;
        case FS_VFAT: partition->target_fs = FS_SWAP; break;
        default:
            if (partition->planned) {
                partition->target_fs = FS_EXT4;
            } else {
                partition->action = ACTION_KEEP;
                partition->target_fs = FS_NONE;
            }
            break;
        }
        return;
    }
    if (partition->planned) {
        if (partition->usage == PART_BOOT) {
            partition->target_fs = FS_VFAT;
            return;
        }
        if (partition->usage == PART_SWAP) {
            partition->target_fs = FS_SWAP;
            return;
        }
        switch (partition->target_fs) {
        case FS_EXT4: partition->target_fs = FS_XFS; break;
        case FS_XFS: partition->target_fs = FS_F2FS; break;
        default: partition->target_fs = FS_EXT4; break;
        }
        partition->action = ACTION_FORMAT;
        return;
    }
    if (partition->action == ACTION_KEEP) {
        partition->action = ACTION_FORMAT;
        partition->target_fs = partition->usage == PART_BOOT ? FS_VFAT :
                               partition->usage == PART_SWAP ? FS_SWAP : FS_EXT4;
        return;
    }
    if (partition->usage == PART_BOOT) {
        partition->action = ACTION_KEEP;
        partition->target_fs = FS_NONE;
        return;
    }
    if (partition->usage == PART_SWAP) {
        partition->action = ACTION_KEEP;
        partition->target_fs = FS_NONE;
        return;
    }
    switch (partition->target_fs) {
    case FS_EXT4: partition->target_fs = FS_XFS; break;
    case FS_XFS: partition->target_fs = FS_F2FS; break;
    default:
        partition->action = ACTION_KEEP;
        partition->target_fs = FS_NONE;
        break;
    }
}

void format_size(uint64_t bytes, char *buffer, size_t size)
{
    const char *unit = "B";
    double value = (double)bytes;
    if (bytes >= GIB) {
        value /= (double)GIB;
        unit = "GiB";
    } else if (bytes >= MIB) {
        value /= (double)MIB;
        unit = "MiB";
    } else if (bytes >= 1024) {
        value /= 1024.0;
        unit = "KiB";
    }
    (void)snprintf(buffer, size, value >= 10.0 ? "%.0f %s" : "%.1f %s", value, unit);
}

static void add_issue(ValidationReport *report, IssueSeverity severity, const char *message)
{
    ValidationIssue *issue;
    if (severity == ISSUE_ERROR) ++report->error_count;
    if (report->count >= AI_MAX_ISSUES) return;
    issue = &report->issues[report->count++];
    issue->severity = severity;
    copy_text(issue->message, sizeof(issue->message), message);
}

static Filesystem effective_filesystem(const PartitionPlan *partition)
{
    return partition->action == ACTION_FORMAT ? partition->target_fs :
           filesystem_from_name(partition->current_fs);
}

static bool is_regular_mount_filesystem(Filesystem filesystem)
{
    return filesystem == FS_EXT4 || filesystem == FS_XFS || filesystem == FS_F2FS;
}

static bool has_automatic_role(const DiskPlan *storage, PartitionUsage usage,
                               unsigned number)
{
    for (size_t index = 0; index < storage->partition_count; ++index) {
        const PartitionPlan *partition = &storage->partitions[index];
        if (partition->usage == usage && partition->number == number &&
            partition->planned && partition->action == ACTION_FORMAT) {
            return true;
        }
    }
    return false;
}

static bool automatic_layout_matches_mode(const DiskPlan *storage)
{
    switch (storage->mode) {
    case STORAGE_AUTO_ROOT_SWAP:
        return storage->partition_count == 3 &&
               has_automatic_role(storage, PART_BOOT, 1) &&
               has_automatic_role(storage, PART_ROOT, 2) &&
               has_automatic_role(storage, PART_SWAP, 3);
    case STORAGE_AUTO_HOME_SWAP:
        return storage->partition_count == 4 &&
               has_automatic_role(storage, PART_BOOT, 1) &&
               has_automatic_role(storage, PART_ROOT, 2) &&
               has_automatic_role(storage, PART_HOME, 3) &&
               has_automatic_role(storage, PART_SWAP, 4);
    case STORAGE_AUTO_ROOT_ONLY:
        return storage->partition_count == 2 &&
               has_automatic_role(storage, PART_BOOT, 1) &&
               has_automatic_role(storage, PART_ROOT, 2);
    case STORAGE_AUTO_DATA:
        return storage->partition_count == 1 &&
               storage->partitions[0].number == 1 &&
               storage->partitions[0].planned &&
               storage->partitions[0].action == ACTION_FORMAT &&
               storage->partitions[0].target_fs != FS_NONE;
    case STORAGE_EXISTING:
        return false;
    }
    return false;
}

void validate_plan(const InstallPlan *plan, ValidationReport *report)
{
    size_t roots = 0;
    size_t boots = 0;
    size_t swaps = 0;
    bool used[PART_SWAP + 1] = {false};
    const DiskPlan *root_disk = NULL;
    const DiskPlan *boot_disk = NULL;
    size_t disk_count = plan->storage.disk_count;

    memset(report, 0, sizeof(*report));
    if (plan->system.platform < PLATFORM_INTEL || plan->system.platform > PLATFORM_VM) {
        add_issue(report, ISSUE_ERROR, "The CPU platform value is invalid.");
    }
    if (plan->system.kernel < KERNEL_LINUX || plan->system.kernel > KERNEL_HARDENED) {
        add_issue(report, ISSUE_ERROR, "The kernel value is invalid.");
    }
    if (plan->system.locale < LOCALE_EN_US || plan->system.locale > LOCALE_ZH_CN) {
        add_issue(report, ISSUE_ERROR, "The locale value is invalid.");
    }
    if (plan->system.desktop < DESKTOP_KDE || plan->system.desktop > DESKTOP_NONE) {
        add_issue(report, ISSUE_ERROR, "The desktop value is invalid.");
    }
    if (disk_count == 0) {
        add_issue(report, ISSUE_ERROR, "Add at least one installation disk.");
    } else if (disk_count > AI_MAX_PLAN_DISKS) {
        add_issue(report, ISSUE_ERROR, "The installation disk list exceeds the supported limit.");
        disk_count = AI_MAX_PLAN_DISKS;
    }

    for (size_t disk_index = 0; disk_index < disk_count; ++disk_index) {
        const DiskPlan *disk = &plan->storage.disks[disk_index];
        size_t partition_count = disk->partition_count;
        uint64_t planned_bytes = 0;
        char message[256];

        if (disk->mode < STORAGE_EXISTING || disk->mode > STORAGE_AUTO_DATA) {
            add_issue(report, ISSUE_ERROR, "A storage disk has an invalid mode.");
        }
        if (!valid_block_device_path(disk->path)) {
            add_issue(report, ISSUE_ERROR, "An installation disk path is not a supported /dev device path.");
        }
        if (disk->size_bytes == 0) {
            add_issue(report, ISSUE_ERROR, "An installation disk has an unknown size.");
        }
        for (size_t previous = 0; previous < disk_index; ++previous) {
            if (strcmp(plan->storage.disks[previous].path, disk->path) == 0) {
                add_issue(report, ISSUE_ERROR, "The same installation disk is referenced more than once.");
                break;
            }
        }
        if (partition_count > AI_MAX_PARTITIONS) {
            add_issue(report, ISSUE_ERROR, "A disk partition plan exceeds the supported limit.");
            partition_count = AI_MAX_PARTITIONS;
        }
        if (disk->removable) {
            (void)snprintf(message, sizeof(message), "Installation disk %.180s is removable.", disk->path);
            add_issue(report, ISSUE_WARNING, message);
        }
        if (disk->read_only) {
            (void)snprintf(message, sizeof(message), "Installation disk %.180s is read-only.", disk->path);
            add_issue(report, ISSUE_ERROR, message);
        }
        if (disk->in_use) {
            (void)snprintf(message, sizeof(message), "Installation disk %.170s currently has mounted partitions.", disk->path);
            add_issue(report, ISSUE_WARNING, message);
        }
        if (disk->path[0] != '\0' && disk->serial[0] == '\0') {
            (void)snprintf(message, sizeof(message), "Installation disk %.150s has no serial number; identity checks are weaker.", disk->path);
            add_issue(report, ISSUE_WARNING, message);
        }
        if (disk->mode != STORAGE_EXISTING) {
            (void)snprintf(message, sizeof(message), "The generated script will erase the partition table on %.170s.", disk->path);
            add_issue(report, ISSUE_WARNING, message);
            if (!automatic_layout_matches_mode(disk)) {
                add_issue(report, ISSUE_ERROR,
                          "An automatic layout no longer matches its fixed partition schema.");
            }
        } else if (disk->path[0] != '\0' && strcasecmp(disk->partition_table, "gpt") != 0) {
            add_issue(report, ISSUE_ERROR,
                      "Using existing partitions requires a GPT partition table on every participating disk.");
        }

        for (size_t index = 0; index < partition_count; ++index) {
            const PartitionPlan *part = &disk->partitions[index];
            Filesystem fs;
            bool actionable;

            if (part->usage < PART_UNUSED || part->usage > PART_SWAP) {
                add_issue(report, ISSUE_ERROR, "A partition has an invalid purpose value.");
                continue;
            }
            if (part->action < ACTION_KEEP || part->action > ACTION_FORMAT) {
                add_issue(report, ISSUE_ERROR, "A partition has an invalid action value.");
                continue;
            }
            if (part->target_fs < FS_NONE || part->target_fs > FS_SWAP) {
                add_issue(report, ISSUE_ERROR, "A partition has an invalid target filesystem value.");
                continue;
            }
            if (part->f2fs_mode < F2FS_DEFAULT || part->f2fs_mode > F2FS_COMPRESSED) {
                add_issue(report, ISSUE_ERROR, "A partition has an invalid F2FS profile value.");
                continue;
            }
            actionable = part->usage != PART_UNUSED || part->action == ACTION_FORMAT || part->planned;
            if (!actionable) continue;
            if (disk->mode == STORAGE_EXISTING && part->planned) {
                add_issue(report, ISSUE_ERROR,
                          "An existing-partition plan cannot contain newly planned partitions.");
            }
            if (part->number == 0) {
                add_issue(report, ISSUE_ERROR, "An active partition has no valid partition number.");
            } else {
                char expected[AI_PATH_LEN];
                partition_device(expected, sizeof(expected), disk->path, part->number);
                if (!valid_block_device_path(part->device) || strcmp(expected, part->device) != 0) {
                    add_issue(report, ISSUE_ERROR, "A partition does not belong to its installation disk.");
                }
            }
            for (size_t other = 0; other < index; ++other) {
                const PartitionPlan *previous = &disk->partitions[other];
                bool previous_active = previous->usage != PART_UNUSED ||
                                       previous->action == ACTION_FORMAT || previous->planned;
                if (!previous_active) continue;
                if (strcmp(previous->device, part->device) == 0 || previous->number == part->number) {
                    add_issue(report, ISSUE_ERROR, "The same partition is referenced more than once.");
                    break;
                }
            }
            if (part->usage == PART_ROOT) { ++roots; root_disk = disk; }
            if (part->usage == PART_BOOT) { ++boots; boot_disk = disk; }
            if (part->usage == PART_SWAP) ++swaps;
            if (part->usage != PART_UNUSED) {
                if (used[part->usage]) {
                    (void)snprintf(message, sizeof(message), "Mount target %s is assigned more than once.",
                                   partition_mountpoint(part->usage));
                    add_issue(report, ISSUE_ERROR, message);
                }
                used[part->usage] = true;
            }
            fs = effective_filesystem(part);
            if (part->action == ACTION_KEEP && fs == FS_NONE) {
                add_issue(report, ISSUE_ERROR, "A kept partition has no recognized filesystem.");
            }
            if (part->action == ACTION_KEEP && part->fs_uuid[0] == '\0') {
                add_issue(report, ISSUE_ERROR, "A kept filesystem is missing its UUID identity.");
            }
            if (part->action == ACTION_KEEP && fs == FS_F2FS &&
                part->f2fs_mode != F2FS_DEFAULT) {
                add_issue(report, ISSUE_ERROR,
                          "A kept F2FS partition must use the compatibility mount profile.");
            }
            if (disk->mode == STORAGE_EXISTING) {
                if (part->size_bytes == 0 || part->start_sector == 0) {
                    add_issue(report, ISSUE_ERROR,
                              "An existing partition is missing its size or start-sector identity.");
                }
                if (part->part_uuid[0] == '\0') {
                    add_issue(report, ISSUE_ERROR,
                              "An existing GPT partition is missing its PARTUUID identity.");
                }
                if (part->part_type[0] == '\0') {
                    add_issue(report, ISSUE_ERROR,
                              "An existing GPT partition is missing its partition type identity.");
                }
            }
            if (part->action == ACTION_FORMAT && part->target_fs == FS_NONE) {
                add_issue(report, ISSUE_ERROR, "A formatted partition has no target filesystem.");
            }
            if (part->planned && part->action != ACTION_FORMAT) {
                add_issue(report, ISSUE_ERROR, "A newly planned partition must be formatted.");
            }
            if (part->size_bytes == 0) {
                add_issue(report, ISSUE_ERROR, "An active partition has no usable space.");
            }
            if (disk->mode != STORAGE_EXISTING && part->planned && part->size_bytes / MIB == 0) {
                add_issue(report, ISSUE_ERROR, "Every automatic partition must be at least 1 MiB.");
            }
            if (part->planned) {
                if (UINT64_MAX - planned_bytes < part->size_bytes) {
                    add_issue(report, ISSUE_ERROR, "Planned partition sizes overflow their supported range.");
                } else {
                    planned_bytes += part->size_bytes;
                }
            }
            if (part->usage == PART_BOOT && fs != FS_VFAT) {
                add_issue(report, ISSUE_ERROR, "The /boot partition must use FAT32/vfat.");
            }
            if (disk->mode == STORAGE_EXISTING && part->usage == PART_BOOT &&
                strcasecmp(part->part_type, GPT_ESP_TYPE) != 0) {
                add_issue(report, ISSUE_ERROR,
                          "The existing /boot partition must have the GPT EFI System type.");
            }
            if (part->usage == PART_SWAP && fs != FS_SWAP) {
                add_issue(report, ISSUE_ERROR, "A swap target must use the swap filesystem.");
            }
            if (part->usage != PART_UNUSED && part->usage != PART_SWAP && fs == FS_SWAP) {
                add_issue(report, ISSUE_ERROR, "A mounted filesystem cannot be swap.");
            }
            if (part->usage != PART_UNUSED && part->usage != PART_BOOT &&
                part->usage != PART_SWAP && !is_regular_mount_filesystem(fs)) {
                add_issue(report, ISSUE_ERROR,
                          "Mounted system partitions must use ext4, XFS, or F2FS.");
            }
            if ((part->usage == PART_ROOT || part->usage == PART_VAR ||
                 part->usage == PART_USR) && part->action != ACTION_FORMAT) {
                add_issue(report, ISSUE_ERROR,
                          "Root, /var, and /usr partitions must be formatted for a new installation.");
            }
            if (part->action == ACTION_KEEP && part->usage != PART_HOME) {
                add_issue(report, ISSUE_WARNING,
                          "KEEP avoids mkfs, but installation can overwrite files under that mount target.");
            }
            if (part->usage == PART_ROOT && part->size_bytes < UINT64_C(8) * GIB) {
                add_issue(report, ISSUE_ERROR, "The root partition must be at least 8 GiB.");
            } else if (part->usage == PART_ROOT && part->size_bytes < UINT64_C(20) * GIB) {
                add_issue(report, ISSUE_WARNING, "The root partition is smaller than 20 GiB.");
            }
            if (part->usage == PART_BOOT && part->size_bytes < UINT64_C(256) * MIB) {
                add_issue(report, ISSUE_ERROR, "The EFI partition must be at least 256 MiB.");
            } else if (part->usage == PART_BOOT && part->size_bytes < UINT64_C(512) * MIB) {
                add_issue(report, ISSUE_WARNING, "The EFI partition is smaller than 512 MiB.");
            }
            if (disk->mode != STORAGE_EXISTING && disk->mode != STORAGE_AUTO_DATA &&
                part->usage == PART_BOOT && part->size_bytes != GIB) {
                add_issue(report, ISSUE_ERROR,
                          "Guided layouts require the fixed 1 GiB EFI partition.");
            }
            if (disk->mode == STORAGE_AUTO_HOME_SWAP && part->usage == PART_ROOT &&
                part->size_bytes != UINT64_C(100) * GIB) {
                add_issue(report, ISSUE_ERROR,
                          "The guided root + home layout requires a 100 GiB root partition.");
            }
            if (disk->mode == STORAGE_AUTO_HOME_SWAP && part->usage == PART_HOME &&
                part->size_bytes < UINT64_C(8) * GIB) {
                add_issue(report, ISSUE_ERROR,
                          "The guided home partition must be at least 8 GiB.");
            }
        }
        if (disk->mode == STORAGE_AUTO_HOME_SWAP &&
            disk->size_bytes < UINT64_C(110) * GIB + recommended_swap_bytes()) {
            add_issue(report, ISSUE_ERROR, "A disk is too small for the 100 GiB root + home layout.");
        }
        if (disk->mode != STORAGE_EXISTING && planned_bytes > disk->size_bytes) {
            add_issue(report, ISSUE_ERROR, "Planned partitions exceed an installation disk's capacity.");
        } else if (disk->mode != STORAGE_EXISTING && planned_bytes != disk->size_bytes) {
            add_issue(report, ISSUE_ERROR,
                      "Automatic partition sizes must account for the entire installation disk.");
        }
    }
    if (roots != 1) add_issue(report, ISSUE_ERROR, "Exactly one root (/) partition is required.");
    if (boots != 1) add_issue(report, ISSUE_ERROR, "Exactly one EFI (/boot) partition is required.");
    if (roots == 1 && boots == 1 && root_disk != boot_disk) {
        add_issue(report, ISSUE_ERROR, "The root (/) and EFI (/boot) partitions must be on the same disk.");
    }
    if (swaps == 0) add_issue(report, ISSUE_WARNING, "No swap partition is configured.");
    if (!valid_hostname(plan->system.hostname)) {
        add_issue(report, ISSUE_ERROR, "Hostname is empty or contains unsupported characters.");
    }
    if (!valid_username(plan->system.username)) {
        add_issue(report, ISSUE_ERROR, "Username is not a valid Linux account name.");
    }
    if (!valid_timezone(plan->system.timezone)) {
        add_issue(report, ISSUE_ERROR, "Timezone must name an available zoneinfo file.");
    }
    if (plan->system.secure_boot) {
        add_issue(report, ISSUE_WARNING,
                  "Secure Boot requires shim-signed.pkg.tar.zst and secure-boot/ beside the builder.");
        add_issue(report, ISSUE_WARNING,
                  "The shim/MOK mode signs EFI binaries and the kernel, not the external initramfs.");
    }
    if (plan->system.secure_boot && plan->system.nvidia_graphics) {
        add_issue(report, ISSUE_WARNING,
                  "Secure Boot does not sign NVIDIA DKMS modules automatically.");
    }
    if (!plan->system.create_efi_entry) {
        add_issue(report, ISSUE_WARNING, "No EFI NVRAM entry will be created.");
    }
    if (plan->system.local_mirror && !plan->system.china_mirrors) {
        add_issue(report, ISSUE_ERROR,
                  "Select a permanent target mirror when using the temporary local mirror.");
    }
}

static const char *json_string(struct json_object *object, const char *key, const char *fallback)
{
    struct json_object *value = NULL;
    if (object != NULL && json_object_object_get_ex(object, key, &value) &&
        json_object_is_type(value, json_type_string)) {
        return json_object_get_string(value);
    }
    return fallback;
}

static bool json_boolean_value(struct json_object *object, const char *key, bool fallback)
{
    struct json_object *value = NULL;
    if (object != NULL && json_object_object_get_ex(object, key, &value)) {
        return json_object_get_boolean(value) != 0;
    }
    return fallback;
}

static int json_int(struct json_object *object, const char *key, int fallback)
{
    struct json_object *value = NULL;
    if (object != NULL && json_object_object_get_ex(object, key, &value)) {
        return json_object_get_int(value);
    }
    return fallback;
}

static void add_bool(struct json_object *object, const char *key, bool value)
{
    json_object_object_add(object, key, json_object_new_boolean(value));
}

typedef struct {
    const char *name;
    enum json_type type;
} JsonField;

static bool require_json_fields(struct json_object *object, const char *section,
                                const JsonField *fields, size_t count,
                                char *error, size_t error_size)
{
    for (size_t index = 0; index < count; ++index) {
        struct json_object *value = NULL;
        if (!json_object_object_get_ex(object, fields[index].name, &value) ||
            !json_object_is_type(value, fields[index].type)) {
            (void)snprintf(error, error_size, "invalid or missing JSON field %s.%s",
                           section, fields[index].name);
            return false;
        }
    }
    return true;
}

static bool require_json_int_range(struct json_object *object, const char *section,
                                   const char *name, int minimum, int maximum,
                                   char *error, size_t error_size)
{
    struct json_object *value = NULL;
    int number;
    if (!json_object_object_get_ex(object, name, &value) ||
        !json_object_is_type(value, json_type_int)) return false;
    number = json_object_get_int(value);
    if (number < minimum || number > maximum) {
        (void)snprintf(error, error_size, "JSON field %s.%s is outside its valid range",
                       section, name);
        return false;
    }
    return true;
}

static bool require_json_nonnegative(struct json_object *object, const char *section,
                                     const char *name, int64_t maximum,
                                     char *error, size_t error_size)
{
    struct json_object *value = NULL;
    int64_t number;

    if (!json_object_object_get_ex(object, name, &value) ||
        !json_object_is_type(value, json_type_int)) return false;
    number = json_object_get_int64(value);
    if (number < 0 || number > maximum) {
        (void)snprintf(error, error_size,
                       "JSON field %s.%s must be a non-negative integer",
                       section, name);
        return false;
    }
    return true;
}

static bool validate_partition_array(struct json_object *parts, const char *prefix,
                                     char *error, size_t error_size)
{
    static const JsonField partition_fields[] = {
        {"device", json_type_string}, {"number", json_type_int},
        {"size_bytes", json_type_int}, {"current_fs", json_type_string},
        {"fs_uuid", json_type_string},
        {"part_uuid", json_type_string}, {"part_type", json_type_string},
        {"start_sector", json_type_int}, {"planned", json_type_boolean},
        {"usage", json_type_int}, {"action", json_type_int},
        {"target_fs", json_type_int}, {"f2fs_mode", json_type_int}
    };

    if (!json_object_is_type(parts, json_type_array)) {
        (void)snprintf(error, error_size, "%s must be an array", prefix);
        return false;
    }
    if (json_object_array_length(parts) > AI_MAX_PARTITIONS) {
        (void)snprintf(error, error_size, "%s exceeds the supported limit", prefix);
        return false;
    }
    for (size_t index = 0; index < json_object_array_length(parts); ++index) {
        struct json_object *item = json_object_array_get_idx(parts, index);
        char section[96];
        if (item == NULL || !json_object_is_type(item, json_type_object)) {
            (void)snprintf(error, error_size, "%s[%zu] must be an object", prefix, index);
            return false;
        }
        (void)snprintf(section, sizeof(section), "%s[%zu]", prefix, index);
        if (!require_json_fields(item, section, partition_fields,
                                 sizeof(partition_fields) / sizeof(partition_fields[0]),
                                 error, error_size) ||
            !require_json_nonnegative(item, section, "number", UINT_MAX, error, error_size) ||
            !require_json_nonnegative(item, section, "size_bytes", INT64_MAX, error, error_size) ||
            !require_json_nonnegative(item, section, "start_sector", INT64_MAX, error, error_size) ||
            !require_json_int_range(item, section, "usage", PART_UNUSED, PART_SWAP,
                                    error, error_size) ||
            !require_json_int_range(item, section, "action", ACTION_KEEP, ACTION_FORMAT,
                                    error, error_size) ||
            !require_json_int_range(item, section, "target_fs", FS_NONE, FS_SWAP,
                                    error, error_size) ||
            !require_json_int_range(item, section, "f2fs_mode", F2FS_DEFAULT,
                                    F2FS_COMPRESSED, error, error_size)) return false;
    }
    return true;
}

static bool validate_json_schema(struct json_object *root,
                                 struct json_object **system_out,
                                 struct json_object **disks_out,
                                 char *error, size_t error_size)
{
    static const JsonField root_fields[] = {
        {"version", json_type_int}, {"storage", json_type_object},
        {"system", json_type_object}
    };
    static const JsonField storage_fields[] = {{"disks", json_type_array}};
    static const JsonField disk_fields[] = {
        {"disk", json_type_string}, {"model", json_type_string},
        {"serial", json_type_string}, {"partition_table", json_type_string},
        {"size_bytes", json_type_int}, {"mode", json_type_int},
        {"removable", json_type_boolean}, {"read_only", json_type_boolean},
        {"in_use_when_detected", json_type_boolean}, {"partitions", json_type_array}
    };
    static const JsonField system_fields[] = {
        {"platform", json_type_int}, {"kernel", json_type_int},
        {"locale", json_type_int}, {"desktop", json_type_int},
        {"timezone", json_type_string}, {"hostname", json_type_string},
        {"username", json_type_string}, {"laptop", json_type_boolean},
        {"intel_graphics", json_type_boolean}, {"nvidia_graphics", json_type_boolean},
        {"bluetooth", json_type_boolean}, {"desktop_recommended", json_type_boolean},
        {"chinese_input", json_type_boolean}, {"firewall", json_type_boolean},
        {"printer", json_type_boolean}, {"archive_tools", json_type_boolean},
        {"terminal_tools", json_type_boolean}, {"extra_tools", json_type_boolean},
        {"desktop_apps", json_type_boolean}, {"local_mirror", json_type_boolean},
        {"china_mirrors", json_type_boolean}, {"secure_boot", json_type_boolean},
        {"create_efi_entry", json_type_boolean}
    };
    struct json_object *storage = NULL;
    struct json_object *system = NULL;
    struct json_object *disks = NULL;

    if (root == NULL || !json_object_is_type(root, json_type_object) ||
        !require_json_fields(root, "root", root_fields,
                             sizeof(root_fields) / sizeof(root_fields[0]), error, error_size)) {
        return false;
    }
    (void)json_object_object_get_ex(root, "storage", &storage);
    (void)json_object_object_get_ex(root, "system", &system);
    if (!require_json_fields(storage, "storage", storage_fields,
                             sizeof(storage_fields) / sizeof(storage_fields[0]), error, error_size) ||
        !require_json_fields(system, "system", system_fields,
                             sizeof(system_fields) / sizeof(system_fields[0]), error, error_size)) {
        return false;
    }
    if (!require_json_int_range(system, "system", "platform", PLATFORM_INTEL,
                                PLATFORM_VM, error, error_size) ||
        !require_json_int_range(system, "system", "kernel", KERNEL_LINUX,
                                KERNEL_HARDENED, error, error_size) ||
        !require_json_int_range(system, "system", "locale", LOCALE_EN_US,
                                LOCALE_ZH_CN, error, error_size) ||
        !require_json_int_range(system, "system", "desktop", DESKTOP_KDE,
                                DESKTOP_NONE, error, error_size)) return false;
    (void)json_object_object_get_ex(storage, "disks", &disks);
    if (json_object_array_length(disks) > AI_MAX_PLAN_DISKS) {
        (void)snprintf(error, error_size, "storage.disks exceeds the supported limit");
        return false;
    }
    for (size_t index = 0; index < json_object_array_length(disks); ++index) {
        struct json_object *disk = json_object_array_get_idx(disks, index);
        struct json_object *parts = NULL;
        char section[64];
        char prefix[80];
        if (disk == NULL || !json_object_is_type(disk, json_type_object)) {
            (void)snprintf(error, error_size, "storage.disks[%zu] must be an object", index);
            return false;
        }
        (void)snprintf(section, sizeof(section), "storage.disks[%zu]", index);
        if (!require_json_fields(disk, section, disk_fields,
                                 sizeof(disk_fields) / sizeof(disk_fields[0]), error, error_size) ||
            !require_json_int_range(disk, section, "mode", STORAGE_EXISTING,
                                    STORAGE_AUTO_DATA, error, error_size) ||
            !require_json_nonnegative(disk, section, "size_bytes", INT64_MAX,
                                      error, error_size)) return false;
        (void)json_object_object_get_ex(disk, "partitions", &parts);
        (void)snprintf(prefix, sizeof(prefix), "storage.disks[%zu].partitions", index);
        if (!validate_partition_array(parts, prefix, error, error_size)) return false;
    }
    *system_out = system;
    *disks_out = disks;
    return true;
}

bool plan_save_json(const InstallPlan *plan, const char *path, char *error, size_t error_size)
{
    struct json_object *root = json_object_new_object();
    struct json_object *storage = json_object_new_object();
    struct json_object *system = json_object_new_object();
    struct json_object *disks = json_object_new_array();
    const char *serialized;
    char *temporary = NULL;
    int descriptor = -1;
    int path_result;
    struct stat status;
    if (root == NULL || storage == NULL || system == NULL || disks == NULL) {
        (void)snprintf(error, error_size, "out of memory while creating JSON");
        if (root != NULL) json_object_put(root);
        return false;
    }
    json_object_object_add(root, "version", json_object_new_int((int)plan->version));
    for (size_t disk_index = 0; disk_index < plan->storage.disk_count; ++disk_index) {
        const DiskPlan *disk = &plan->storage.disks[disk_index];
        struct json_object *disk_object = json_object_new_object();
        struct json_object *partitions = json_object_new_array();
        if (disk_object == NULL || partitions == NULL) {
            if (disk_object != NULL) json_object_put(disk_object);
            if (partitions != NULL) json_object_put(partitions);
            (void)snprintf(error, error_size, "out of memory while creating disk JSON");
            json_object_put(root);
            return false;
        }
        json_object_object_add(disk_object, "disk", json_object_new_string(disk->path));
        json_object_object_add(disk_object, "model", json_object_new_string(disk->model));
        json_object_object_add(disk_object, "serial", json_object_new_string(disk->serial));
        json_object_object_add(disk_object, "partition_table",
                               json_object_new_string(disk->partition_table));
        json_object_object_add(disk_object, "size_bytes", json_object_new_uint64(disk->size_bytes));
        json_object_object_add(disk_object, "mode", json_object_new_int((int)disk->mode));
        add_bool(disk_object, "removable", disk->removable);
        add_bool(disk_object, "read_only", disk->read_only);
        add_bool(disk_object, "in_use_when_detected", disk->in_use);
        for (size_t index = 0; index < disk->partition_count; ++index) {
            const PartitionPlan *part = &disk->partitions[index];
            struct json_object *item = json_object_new_object();
            json_object_object_add(item, "device", json_object_new_string(part->device));
            json_object_object_add(item, "number", json_object_new_int64((int64_t)part->number));
            json_object_object_add(item, "size_bytes", json_object_new_uint64(part->size_bytes));
            json_object_object_add(item, "current_fs", json_object_new_string(part->current_fs));
            json_object_object_add(item, "fs_uuid", json_object_new_string(part->fs_uuid));
            json_object_object_add(item, "part_uuid", json_object_new_string(part->part_uuid));
            json_object_object_add(item, "part_type", json_object_new_string(part->part_type));
            json_object_object_add(item, "start_sector", json_object_new_uint64(part->start_sector));
            add_bool(item, "planned", part->planned);
            json_object_object_add(item, "usage", json_object_new_int((int)part->usage));
            json_object_object_add(item, "action", json_object_new_int((int)part->action));
            json_object_object_add(item, "target_fs", json_object_new_int((int)part->target_fs));
            json_object_object_add(item, "f2fs_mode", json_object_new_int((int)part->f2fs_mode));
            json_object_array_add(partitions, item);
        }
        json_object_object_add(disk_object, "partitions", partitions);
        json_object_array_add(disks, disk_object);
    }
    json_object_object_add(storage, "disks", disks);
    json_object_object_add(root, "storage", storage);

    json_object_object_add(system, "platform", json_object_new_int((int)plan->system.platform));
    json_object_object_add(system, "kernel", json_object_new_int((int)plan->system.kernel));
    json_object_object_add(system, "locale", json_object_new_int((int)plan->system.locale));
    json_object_object_add(system, "desktop", json_object_new_int((int)plan->system.desktop));
    json_object_object_add(system, "timezone", json_object_new_string(plan->system.timezone));
    json_object_object_add(system, "hostname", json_object_new_string(plan->system.hostname));
    json_object_object_add(system, "username", json_object_new_string(plan->system.username));
#define ADD_SYSTEM_BOOL(field) add_bool(system, #field, plan->system.field)
    ADD_SYSTEM_BOOL(laptop); ADD_SYSTEM_BOOL(intel_graphics); ADD_SYSTEM_BOOL(nvidia_graphics);
    ADD_SYSTEM_BOOL(bluetooth); ADD_SYSTEM_BOOL(desktop_recommended); ADD_SYSTEM_BOOL(chinese_input);
    ADD_SYSTEM_BOOL(firewall); ADD_SYSTEM_BOOL(printer); ADD_SYSTEM_BOOL(archive_tools);
    ADD_SYSTEM_BOOL(terminal_tools); ADD_SYSTEM_BOOL(extra_tools); ADD_SYSTEM_BOOL(desktop_apps);
    ADD_SYSTEM_BOOL(local_mirror); ADD_SYSTEM_BOOL(china_mirrors); ADD_SYSTEM_BOOL(secure_boot);
    ADD_SYSTEM_BOOL(create_efi_entry);
#undef ADD_SYSTEM_BOOL
    json_object_object_add(root, "system", system);
    path_result = lstat(path, &status);
    if (path_result == 0 && !S_ISREG(status.st_mode)) {
        (void)snprintf(error, error_size, "refusing to replace non-regular plan path: %s", path);
        json_object_put(root);
        return false;
    }
    if (path_result != 0 && errno != ENOENT) {
        (void)snprintf(error, error_size, "cannot inspect plan path %s: %s", path,
                       strerror(errno));
        json_object_put(root);
        return false;
    }
    temporary = malloc(strlen(path) + 16);
    if (temporary == NULL) {
        (void)snprintf(error, error_size, "out of memory while saving plan");
        json_object_put(root);
        return false;
    }
    (void)snprintf(temporary, strlen(path) + 16, "%s.tmp.XXXXXX", path);
    descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        (void)snprintf(error, error_size, "cannot create temporary plan: %s", strerror(errno));
        free(temporary);
        json_object_put(root);
        return false;
    }
    if (fchmod(descriptor, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) != 0) {
        (void)snprintf(error, error_size, "cannot set plan permissions: %s", strerror(errno));
        (void)close(descriptor);
        (void)unlink(temporary);
        free(temporary);
        json_object_put(root);
        return false;
    }
    serialized = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_SPACED);
    {
        size_t remaining = strlen(serialized);
        const char *cursor = serialized;
        while (remaining > 0) {
            ssize_t written = write(descriptor, cursor, remaining);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) {
                (void)snprintf(error, error_size, "cannot write plan: %s", strerror(errno));
                (void)close(descriptor);
                (void)unlink(temporary);
                free(temporary);
                json_object_put(root);
                return false;
            }
            cursor += written;
            remaining -= (size_t)written;
        }
    }
    if (fsync(descriptor) != 0) {
        (void)snprintf(error, error_size, "cannot commit plan %s: %s", path, strerror(errno));
        (void)close(descriptor);
        (void)unlink(temporary);
        free(temporary);
        json_object_put(root);
        return false;
    }
    if (close(descriptor) != 0) {
        (void)snprintf(error, error_size, "cannot close plan %s: %s", path, strerror(errno));
        (void)unlink(temporary);
        free(temporary);
        json_object_put(root);
        return false;
    }
    if (rename(temporary, path) != 0) {
        (void)snprintf(error, error_size, "cannot commit plan %s: %s", path, strerror(errno));
        (void)unlink(temporary);
        free(temporary);
        json_object_put(root);
        return false;
    }
    free(temporary);
    json_object_put(root);
    return true;
}

static void load_partitions_json(DiskPlan *disk, struct json_object *parts)
{
    struct json_object *value = NULL;
    size_t count = json_object_array_length(parts);

    disk->partition_count = count > AI_MAX_PARTITIONS ? AI_MAX_PARTITIONS : count;
    for (size_t index = 0; index < disk->partition_count; ++index) {
        struct json_object *item = json_object_array_get_idx(parts, index);
        PartitionPlan *part = &disk->partitions[index];
        memset(part, 0, sizeof(*part));
        copy_text(part->device, sizeof(part->device), json_string(item, "device", ""));
        copy_text(part->current_fs, sizeof(part->current_fs), json_string(item, "current_fs", ""));
        copy_text(part->fs_uuid, sizeof(part->fs_uuid), json_string(item, "fs_uuid", ""));
        copy_text(part->part_uuid, sizeof(part->part_uuid), json_string(item, "part_uuid", ""));
        copy_text(part->part_type, sizeof(part->part_type), json_string(item, "part_type", ""));
        part->number = (unsigned)json_int(item, "number", 0);
        if (json_object_object_get_ex(item, "size_bytes", &value))
            part->size_bytes = json_object_get_uint64(value);
        if (json_object_object_get_ex(item, "start_sector", &value))
            part->start_sector = json_object_get_uint64(value);
        part->planned = json_boolean_value(item, "planned", false);
        part->usage = (PartitionUsage)json_int(item, "usage", PART_UNUSED);
        part->action = (PartitionAction)json_int(item, "action", ACTION_KEEP);
        part->target_fs = (Filesystem)json_int(item, "target_fs", FS_NONE);
        part->f2fs_mode = (F2fsMountMode)json_int(item, "f2fs_mode", F2FS_BALANCED);
    }
}

static void load_disk_json(DiskPlan *disk, struct json_object *object,
                           struct json_object *parts)
{
    struct json_object *value = NULL;
    memset(disk, 0, sizeof(*disk));
    copy_text(disk->path, sizeof(disk->path), json_string(object, "disk", ""));
    copy_text(disk->model, sizeof(disk->model), json_string(object, "model", ""));
    copy_text(disk->serial, sizeof(disk->serial), json_string(object, "serial", ""));
    copy_text(disk->partition_table, sizeof(disk->partition_table),
              json_string(object, "partition_table", ""));
    if (json_object_object_get_ex(object, "size_bytes", &value))
        disk->size_bytes = json_object_get_uint64(value);
    disk->mode = (StorageMode)json_int(object, "mode", STORAGE_EXISTING);
    disk->removable = json_boolean_value(object, "removable", false);
    disk->read_only = json_boolean_value(object, "read_only", false);
    disk->in_use = json_boolean_value(object, "in_use_when_detected", false);
    load_partitions_json(disk, parts);
}

bool plan_load_json(InstallPlan *plan, const char *path, char *error, size_t error_size)
{
    struct json_object *root = json_object_from_file(path);
    struct json_object *system = NULL;
    struct json_object *parts = NULL;
    struct json_object *version_value = NULL;
    int64_t version;
    if (root == NULL) {
        (void)snprintf(error, error_size, "cannot parse plan file: %s", path);
        return false;
    }
    if (!json_object_is_type(root, json_type_object) ||
        !json_object_object_get_ex(root, "version", &version_value) ||
        !json_object_is_type(version_value, json_type_int)) {
        (void)snprintf(error, error_size, "invalid or missing JSON field root.version");
        json_object_put(root);
        return false;
    }
    version = json_object_get_int64(version_value);
    if (version != 3) {
        (void)snprintf(error, error_size, "unsupported plan version: %" PRId64, version);
        json_object_put(root);
        return false;
    }
    if (!validate_json_schema(root, &system, &parts, error, error_size)) {
        json_object_put(root);
        return false;
    }
    plan_init(plan);
    plan->version = 3;
    {
        size_t count = json_object_array_length(parts);
        plan->storage.disk_count = count > AI_MAX_PLAN_DISKS ? AI_MAX_PLAN_DISKS : count;
        for (size_t index = 0; index < plan->storage.disk_count; ++index) {
            struct json_object *disk_object = json_object_array_get_idx(parts, index);
            struct json_object *disk_parts = NULL;
            (void)json_object_object_get_ex(disk_object, "partitions", &disk_parts);
            load_disk_json(&plan->storage.disks[index], disk_object, disk_parts);
        }
    }
    {
        int platform = json_int(system, "platform", PLATFORM_INTEL);
        int kernel = json_int(system, "kernel", KERNEL_LINUX);
        int locale = json_int(system, "locale", LOCALE_EN_US);
        int desktop = json_int(system, "desktop", DESKTOP_KDE);
        plan->system.platform = platform >= PLATFORM_INTEL && platform <= PLATFORM_VM ?
                                (Platform)platform : PLATFORM_INTEL;
        plan->system.kernel = kernel >= KERNEL_LINUX && kernel <= KERNEL_HARDENED ?
                              (Kernel)kernel : KERNEL_LINUX;
        plan->system.locale = locale >= LOCALE_EN_US && locale <= LOCALE_ZH_CN ?
                              (LocaleChoice)locale : LOCALE_EN_US;
        plan->system.desktop = desktop >= DESKTOP_KDE && desktop <= DESKTOP_NONE ?
                               (Desktop)desktop : DESKTOP_KDE;
        copy_text(plan->system.timezone, sizeof(plan->system.timezone), json_string(system, "timezone", "Asia/Shanghai"));
        copy_text(plan->system.hostname, sizeof(plan->system.hostname), json_string(system, "hostname", "ARCH-LINUX"));
        copy_text(plan->system.username, sizeof(plan->system.username), json_string(system, "username", "user"));
#define LOAD_SYSTEM_BOOL(field) plan->system.field = json_boolean_value(system, #field, plan->system.field)
        LOAD_SYSTEM_BOOL(laptop); LOAD_SYSTEM_BOOL(intel_graphics); LOAD_SYSTEM_BOOL(nvidia_graphics);
        LOAD_SYSTEM_BOOL(bluetooth); LOAD_SYSTEM_BOOL(desktop_recommended); LOAD_SYSTEM_BOOL(chinese_input);
        LOAD_SYSTEM_BOOL(firewall); LOAD_SYSTEM_BOOL(printer); LOAD_SYSTEM_BOOL(archive_tools);
        LOAD_SYSTEM_BOOL(terminal_tools); LOAD_SYSTEM_BOOL(extra_tools); LOAD_SYSTEM_BOOL(desktop_apps);
        LOAD_SYSTEM_BOOL(local_mirror); LOAD_SYSTEM_BOOL(china_mirrors); LOAD_SYSTEM_BOOL(secure_boot);
        LOAD_SYSTEM_BOOL(create_efi_entry);
#undef LOAD_SYSTEM_BOOL
    }
    json_object_put(root);
    return true;
}
