#include "model.h"

#include "private.h"
#include "text.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define MIB (UINT64_C(1024) * UINT64_C(1024))
#define GIB (UINT64_C(1024) * UINT64_C(1024) * UINT64_C(1024))
#define GPT_ESP_TYPE "c12a7328-f81f-11d2-ba4b-00a0c93ec93b"

/* 基础辅助检查：设备路径语法、问题收集和分区最终文件系统推导。 */
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

static void add_issue(ValidationReport *report, IssueSeverity severity, const char *message)
{
    ValidationIssue *issue;
    /* 展示缓冲区满后仍继续累计错误数，不能因 UI 容量上限误判方案为可执行。 */
    if (severity == ISSUE_ERROR) ++report->error_count;
    if (report->count >= AI_MAX_ISSUES) return;
    issue = &report->issues[report->count++];
    issue->severity = severity;
    copy_text(issue->message, sizeof(issue->message), message);
}

/* 引导式布局必须继续符合固定分区数量、编号、用途和格式化动作。 */
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
    /*
     * 引导式布局虽然由 UI 生成，仍可能来自手工修改的 JSON；在此重新核对
     * 固定角色可防止生成器按某个模式输出与实际分区数组不一致的 sfdisk 配置。
     */
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

/*
 * 集中验证所有会影响脚本正确性和设备身份的约束。函数尽量收集完整报告，
 * 而不是遇到第一项错误就返回，方便 Review 页面一次展示所有问题。
 */
void validate_plan(const InstallPlan *plan, ValidationReport *report)
{
    size_t roots = 0;
    size_t boots = 0;
    size_t swaps = 0;
    bool used[PART_SWAP + 1] = {false};
    const DiskPlan *root_disk = NULL;
    const DiskPlan *boot_disk = NULL;
    size_t disk_count = plan->storage.disk_count;

    /* 先检查系统枚举和顶层磁盘数量，防止后续索引越过模型边界。 */
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

    /* 每块磁盘先验证身份与布局模式，再验证其中所有需要处理的分区。 */
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

        /*
         * 分区检查覆盖设备归属、重复引用、挂载点唯一性、KEEP 身份、
         * 文件系统兼容性、容量下限以及引导式布局的固定尺寸。
         */
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
            if (part->mount_profile < MOUNT_PROFILE_DEFAULT ||
                part->mount_profile > MOUNT_PROFILE_COMPRESSED) {
                add_issue(report, ISSUE_ERROR, "A partition has an invalid mount profile value.");
                continue;
            }
            /* 未使用且保持原样的现有分区不进入安装流程，其余三类都必须完整校验。 */
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
                /* 用磁盘路径和分区号重建名称，阻止分区记录悄悄指向另一块盘。 */
                model_partition_device(expected, sizeof(expected), disk->path, part->number);
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
                /* 用途枚举直接充当唯一挂载目标索引，因此可一次覆盖所有磁盘。 */
                if (used[part->usage]) {
                    (void)snprintf(message, sizeof(message), "Mount target %s is assigned more than once.",
                                   partition_mountpoint(part->usage));
                    add_issue(report, ISSUE_ERROR, message);
                }
                used[part->usage] = true;
            }
            fs = partition_effective_filesystem(part);
            /*
             * KEEP 同时依赖文件系统 UUID（挂载身份）和下方的 GPT PARTUUID、
             * 起始扇区及容量（分区身份）；两层身份解决的问题不同，不能互相替代。
             */
            if (part->action == ACTION_KEEP && fs == FS_NONE) {
                add_issue(report, ISSUE_ERROR, "A kept partition has no recognized filesystem.");
            }
            if (part->action == ACTION_KEEP && part->fs_uuid[0] == '\0') {
                add_issue(report, ISSUE_ERROR, "A kept filesystem is missing its UUID identity.");
            }
            /* 当前只有待格式化且实际挂载的 F2FS 支持非默认挂载选项。 */
            if (part->mount_profile != MOUNT_PROFILE_DEFAULT &&
                !partition_supports_mount_profile(part, part->mount_profile)) {
                add_issue(report, ISSUE_ERROR,
                          "Non-default mount options currently require a formatted and mounted F2FS partition.");
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
                /* 先防止无符号加法回绕，再用总和检查自动布局是否覆盖整盘。 */
                if (UINT64_MAX - planned_bytes < part->size_bytes) {
                    add_issue(report, ISSUE_ERROR, "Planned partition sizes overflow their supported range.");
                } else {
                    planned_bytes += part->size_bytes;
                }
            }
            if (part->usage == PART_BOOT && fs != FS_VFAT) {
                /* 用途约束基于 effective filesystem，因而同时覆盖 KEEP 与 FORMAT。 */
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
                part->usage != PART_SWAP && !filesystem_is_regular(fs)) {
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
            /* 模型必须精确记满整盘；GPT 对齐余量只在脚本序列化阶段扣除。 */
            add_issue(report, ISSUE_ERROR,
                      "Automatic partition sizes must account for the entire installation disk.");
        }
    }

    /* 最后检查跨磁盘的全局不变量以及系统文本和选项之间的组合约束。 */
    if (roots != 1) add_issue(report, ISSUE_ERROR, "Exactly one root (/) partition is required.");
    if (boots != 1) add_issue(report, ISSUE_ERROR, "Exactly one EFI (/boot) partition is required.");
    /* 指针指向各自所属 DiskPlan，直接比较即可落实跨盘的 root/boot 共盘约束。 */
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
