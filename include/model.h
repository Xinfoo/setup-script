#ifndef ARCH_INSTALLER_MODEL_H
#define ARCH_INSTALLER_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AI_MAX_DISKS 32
#define AI_MAX_PARTITIONS 128
#define AI_MAX_ISSUES 32
#define AI_PATH_LEN 256
#define AI_TEXT_LEN 128

typedef enum {
    FS_NONE,
    FS_VFAT,
    FS_EXT4,
    FS_XFS,
    FS_F2FS,
    FS_SWAP
} Filesystem;

typedef enum {
    PART_UNUSED,
    PART_ROOT,
    PART_BOOT,
    PART_HOME,
    PART_VAR,
    PART_USR,
    PART_OPT,
    PART_SWAP
} PartitionUsage;

typedef enum {
    ACTION_KEEP,
    ACTION_FORMAT
} PartitionAction;

typedef enum {
    STORAGE_EXISTING,
    STORAGE_AUTO_ROOT_SWAP,
    STORAGE_AUTO_HOME_SWAP,
    STORAGE_AUTO_ROOT_ONLY
} StorageMode;

typedef enum { PLATFORM_INTEL, PLATFORM_AMD, PLATFORM_VM } Platform;
typedef enum { KERNEL_LINUX, KERNEL_LTS, KERNEL_ZEN, KERNEL_HARDENED } Kernel;
typedef enum { DESKTOP_KDE, DESKTOP_GNOME, DESKTOP_HYPRLAND, DESKTOP_NONE } Desktop;
typedef enum { LOCALE_EN_US, LOCALE_ZH_CN } LocaleChoice;
typedef enum { F2FS_DEFAULT, F2FS_BALANCED, F2FS_COMPRESSED } F2fsMountMode;

typedef struct {
    char path[AI_PATH_LEN];
    char current_fs[32];
    char fs_uuid[AI_TEXT_LEN];
    char label[AI_TEXT_LEN];
    char mountpoint[AI_PATH_LEN];
    char part_uuid[AI_TEXT_LEN];
    char part_type[64];
    uint64_t size_bytes;
    uint64_t start_sector;
    unsigned number;
} PartitionInfo;

typedef struct {
    char name[64];
    char path[AI_PATH_LEN];
    char model[AI_TEXT_LEN];
    char serial[AI_TEXT_LEN];
    char transport[32];
    char partition_table[32];
    uint64_t size_bytes;
    bool removable;
    bool read_only;
    bool in_use;
    PartitionInfo partitions[AI_MAX_PARTITIONS];
    size_t partition_count;
} DiskInfo;

typedef struct {
    DiskInfo disks[AI_MAX_DISKS];
    size_t disk_count;
} HardwareInventory;

typedef struct {
    char device[AI_PATH_LEN];
    char current_fs[32];
    char fs_uuid[AI_TEXT_LEN];
    char part_uuid[AI_TEXT_LEN];
    char part_type[64];
    uint64_t size_bytes;
    uint64_t start_sector;
    unsigned number;
    bool planned;
    PartitionUsage usage;
    PartitionAction action;
    Filesystem target_fs;
    F2fsMountMode f2fs_mode;
} PartitionPlan;

typedef struct {
    char disk_path[AI_PATH_LEN];
    char disk_model[AI_TEXT_LEN];
    char disk_serial[AI_TEXT_LEN];
    char partition_table[32];
    uint64_t disk_size_bytes;
    bool disk_removable;
    bool disk_read_only;
    bool disk_in_use;
    StorageMode mode;
    PartitionPlan partitions[AI_MAX_PARTITIONS];
    size_t partition_count;
} StoragePlan;

typedef struct {
    Platform platform;
    Kernel kernel;
    LocaleChoice locale;
    Desktop desktop;
    char timezone[AI_TEXT_LEN];
    char hostname[AI_TEXT_LEN];
    char username[AI_TEXT_LEN];
    bool laptop;
    bool intel_graphics;
    bool nvidia_graphics;
    bool bluetooth;
    bool desktop_recommended;
    bool chinese_input;
    bool firewall;
    bool printer;
    bool archive_tools;
    bool terminal_tools;
    bool extra_tools;
    bool desktop_apps;
    bool local_mirror;
    bool china_mirrors;
    bool secure_boot;
    bool create_efi_entry;
} SystemPlan;

typedef struct {
    unsigned version;
    StoragePlan storage;
    SystemPlan system;
} InstallPlan;

typedef enum { ISSUE_WARNING, ISSUE_ERROR } IssueSeverity;

typedef struct {
    IssueSeverity severity;
    char message[256];
} ValidationIssue;

typedef struct {
    ValidationIssue issues[AI_MAX_ISSUES];
    size_t count;
    size_t error_count;
} ValidationReport;

void plan_init(InstallPlan *plan);
void plan_select_disk(InstallPlan *plan, const DiskInfo *disk);
void plan_use_existing(InstallPlan *plan, const DiskInfo *disk);
void plan_use_automatic(InstallPlan *plan, const DiskInfo *disk, StorageMode mode);
void plan_cycle_usage(PartitionPlan *partition);
void plan_cycle_format(PartitionPlan *partition);
const char *filesystem_name(Filesystem value);
const char *usage_name(PartitionUsage value);
const char *action_name(PartitionAction value);
const char *storage_mode_name(StorageMode value);
const char *platform_name(Platform value);
const char *kernel_name(Kernel value);
const char *desktop_name(Desktop value);
const char *locale_name(LocaleChoice value);
const char *f2fs_mode_name(F2fsMountMode value);
const char *partition_mountpoint(PartitionUsage value);
Filesystem filesystem_from_name(const char *name);
uint64_t recommended_swap_bytes(void);
void format_size(uint64_t bytes, char *buffer, size_t size);
void validate_plan(const InstallPlan *plan, ValidationReport *report);
bool plan_save_json(const InstallPlan *plan, const char *path, char *error, size_t error_size);
bool plan_load_json(InstallPlan *plan, const char *path, char *error, size_t error_size);

#endif
