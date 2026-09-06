#ifndef ARCH_INSTALLER_MODEL_H
#define ARCH_INSTALLER_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 固定容量上限既约束内存布局，也用于 JSON 和 TUI 的边界检查。 */
#define AI_MAX_DISKS 32
#define AI_MAX_PLAN_DISKS 8
#define AI_MAX_PARTITIONS 128
#define AI_MAX_ISSUES 32
#define AI_PATH_LEN 256
#define AI_TEXT_LEN 128
#define AI_PLAN_VERSION 4U

/* 分区文件系统、用途、执行动作、挂载配置档和磁盘布局模式。 */
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
    STORAGE_AUTO_ROOT_ONLY,
    STORAGE_AUTO_DATA
} StorageMode;

typedef enum { PLATFORM_INTEL, PLATFORM_AMD, PLATFORM_VM } Platform;
typedef enum { KERNEL_LINUX, KERNEL_LTS, KERNEL_ZEN, KERNEL_HARDENED } Kernel;
typedef enum { DESKTOP_KDE, DESKTOP_GNOME, DESKTOP_HYPRLAND, DESKTOP_NONE } Desktop;
typedef enum { LOCALE_EN_US, LOCALE_ZH_CN } LocaleChoice;
typedef enum {
    MOUNT_PROFILE_DEFAULT,
    MOUNT_PROFILE_BALANCED,
    MOUNT_PROFILE_COMPRESSED
} MountProfile;

/* 硬件探测快照：只描述当前系统看到的磁盘和分区，不表达安装意图。 */
typedef struct {
    char path[AI_PATH_LEN];
    char current_fs[32];
    char fs_uuid[AI_TEXT_LEN];
    char part_uuid[AI_TEXT_LEN];
    char part_type[64];
    uint64_t size_bytes;
    uint64_t start_sector;
    unsigned number;
} PartitionInfo;

typedef struct {
    char path[AI_PATH_LEN];
    char model[AI_TEXT_LEN];
    char serial[AI_TEXT_LEN];
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

/*
 * 安装方案中的分区。planned 表示由引导式布局新建；usage/action/target_fs
 * 分别描述挂载用途、是否格式化以及格式化后的文件系统，mount_profile 保存
 * 与最终文件系统配套的挂载配置档。
 */
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
    MountProfile mount_profile;
} PartitionPlan;

typedef struct {
    char path[AI_PATH_LEN];
    char model[AI_TEXT_LEN];
    char serial[AI_TEXT_LEN];
    char partition_table[32];
    uint64_t size_bytes;
    bool removable;
    bool read_only;
    bool in_use;
    StorageMode mode;
    PartitionPlan partitions[AI_MAX_PARTITIONS];
    size_t partition_count;
} DiskPlan;

typedef struct {
    DiskPlan disks[AI_MAX_PLAN_DISKS];
    size_t disk_count;
} StoragePlan;

/* 与存储无关的系统、硬件支持、软件组和启动选项。 */
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

/* 验证会同时收集警告和阻塞错误，error_count 只统计后者。 */
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

/* 方案构造与磁盘布局。 */
void plan_init(InstallPlan *plan);
bool plan_add_disk(InstallPlan *plan, const DiskInfo *disk);
DiskPlan *plan_find_disk(InstallPlan *plan, const char *path);
void disk_plan_use_existing(DiskPlan *plan, const DiskInfo *disk);
void disk_plan_use_automatic(DiskPlan *plan, const DiskInfo *disk, StorageMode mode);

/* 枚举与界面、JSON 及 Shell 使用的稳定文本之间的转换。 */
const char *filesystem_name(Filesystem value);
const char *usage_name(PartitionUsage value);
const char *action_name(PartitionAction value);
const char *storage_mode_name(StorageMode value);
const char *platform_name(Platform value);
const char *kernel_name(Kernel value);
const char *desktop_name(Desktop value);
const char *locale_name(LocaleChoice value);
const char *mount_profile_name(MountProfile value);
const char *partition_mountpoint(PartitionUsage value);
Filesystem filesystem_from_name(const char *name);

/* 分区动作推导和普通挂载格式规则由模型层统一解释。 */
Filesystem partition_effective_filesystem(const PartitionPlan *partition);
bool filesystem_is_regular(Filesystem filesystem);
bool partition_supports_mount_profile(const PartitionPlan *partition,
                                      MountProfile profile);
uint64_t recommended_swap_bytes(void);
void format_size(uint64_t bytes, char *buffer, size_t size);

/* 集中验证和版本严格的 JSON 持久化接口。 */
void validate_plan(const InstallPlan *plan, ValidationReport *report);
bool plan_save_json(const InstallPlan *plan, const char *path, char *error, size_t error_size);
bool plan_load_json(InstallPlan *plan, const char *path, char *error, size_t error_size);

#endif
