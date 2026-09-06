#define _POSIX_C_SOURCE 200809L

#include "model.h"

#include "private.h"
#include "text.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#define MIB (UINT64_C(1024) * UINT64_C(1024))
#define GIB (UINT64_C(1024) * UINT64_C(1024) * UINT64_C(1024))

/* 根据磁盘名是否以数字结尾，生成 sdX1 或 nvme0n1p1 形式的分区设备名。 */
void model_partition_device(char *output, size_t size, const char *disk, unsigned number)
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

DiskPlan *plan_find_disk(InstallPlan *plan, const char *path)
{
    for (size_t index = 0; index < plan->storage.disk_count; ++index) {
        if (strcmp(plan->storage.disks[index].path, path) == 0) {
            return &plan->storage.disks[index];
        }
    }
    return NULL;
}

const DiskPlan *plan_find_disk_for_usage(const InstallPlan *plan, PartitionUsage usage)
{
    if (plan == NULL) return NULL;
    for (size_t disk_index = 0; disk_index < plan->storage.disk_count; ++disk_index) {
        const DiskPlan *disk = &plan->storage.disks[disk_index];
        for (size_t partition_index = 0;
             partition_index < disk->partition_count; ++partition_index) {
            if (disk->partitions[partition_index].usage == usage) return disk;
        }
    }
    return NULL;
}

bool plan_remove_disk_at(InstallPlan *plan, size_t index)
{
    StoragePlan *storage;

    if (plan == NULL || index >= plan->storage.disk_count) return false;
    storage = &plan->storage;
    if (index + 1 < storage->disk_count) {
        memmove(&storage->disks[index], &storage->disks[index + 1],
                (storage->disk_count - index - 1) * sizeof(storage->disks[0]));
    }
    --storage->disk_count;
    memset(&storage->disks[storage->disk_count], 0, sizeof(storage->disks[0]));
    return true;
}

static const DiskInfo *find_inventory_disk(const HardwareInventory *inventory,
                                           const char *path)
{
    if (inventory == NULL) return NULL;
    for (size_t index = 0; index < inventory->disk_count; ++index) {
        if (strcmp(inventory->disks[index].path, path) == 0) {
            return &inventory->disks[index];
        }
    }
    return NULL;
}

/*
 * 生成前把方案所依据的探测快照与当前设备重新比对。自动布局只依赖整盘
 * 身份；复用现有分区时还要确认边界、GPT 身份以及 KEEP 文件系统未变化。
 */
bool plan_storage_matches_inventory(const InstallPlan *plan,
                                    const HardwareInventory *inventory)
{
    if (plan == NULL || inventory == NULL) return false;
    for (size_t disk_index = 0; disk_index < plan->storage.disk_count; ++disk_index) {
        const DiskPlan *planned_disk = &plan->storage.disks[disk_index];
        const DiskInfo *disk = find_inventory_disk(inventory, planned_disk->path);

        if (disk == NULL || disk->read_only || disk->size_bytes != planned_disk->size_bytes)
            return false;
        if (planned_disk->serial[0] != '\0' &&
            strcmp(disk->serial, planned_disk->serial) != 0) return false;
        if (planned_disk->model[0] != '\0' &&
            strcmp(disk->model, planned_disk->model) != 0) return false;
        if (planned_disk->mode != STORAGE_EXISTING) continue;
        if (strcasecmp(disk->partition_table, planned_disk->partition_table) != 0)
            return false;

        for (size_t index = 0; index < planned_disk->partition_count; ++index) {
            const PartitionPlan *planned = &planned_disk->partitions[index];
            const PartitionInfo *current = NULL;

            if (planned->usage == PART_UNUSED && planned->action != ACTION_FORMAT) continue;
            for (size_t part = 0; part < disk->partition_count; ++part) {
                if (disk->partitions[part].number == planned->number &&
                    strcmp(disk->partitions[part].path, planned->device) == 0) {
                    current = &disk->partitions[part];
                    break;
                }
            }
            if (current == NULL || current->size_bytes != planned->size_bytes ||
                current->start_sector != planned->start_sector) return false;
            if (planned->part_uuid[0] != '\0' &&
                strcasecmp(current->part_uuid, planned->part_uuid) != 0) return false;
            if (planned->part_type[0] != '\0' &&
                strcasecmp(current->part_type, planned->part_type) != 0) return false;
            if (planned->action == ACTION_KEEP &&
                filesystem_from_name(current->current_fs) !=
                filesystem_from_name(planned->current_fs)) return false;
            if (planned->action == ACTION_KEEP &&
                strcasecmp(current->fs_uuid, planned->fs_uuid) != 0) return false;
        }
    }
    return true;
}

/* 根据用途统一推导默认格式化动作，UI 只负责选择和后续确认。 */
bool partition_plan_assign_usage(PartitionPlan *partition, StorageMode mode,
                                 PartitionUsage usage)
{
    Filesystem current;

    if (partition == NULL || usage < PART_UNUSED || usage > PART_SWAP) return false;
    if (partition->planned && mode != STORAGE_AUTO_DATA) return false;
    partition->usage = usage;
    current = filesystem_from_name(partition->current_fs);
    if (usage == PART_UNUSED) {
        partition->mount_profile = MOUNT_PROFILE_DEFAULT;
        if (partition->planned && mode == STORAGE_AUTO_DATA) {
            partition->action = ACTION_FORMAT;
            if (partition->target_fs == FS_NONE) partition->target_fs = FS_EXT4;
        } else {
            partition->action = ACTION_KEEP;
            partition->target_fs = FS_NONE;
        }
    } else if (usage == PART_ROOT || usage == PART_VAR || usage == PART_USR) {
        partition->action = ACTION_FORMAT;
        partition->target_fs = filesystem_is_regular(current) ? current : FS_EXT4;
    } else if (usage == PART_BOOT) {
        partition->action = current == FS_VFAT ? ACTION_KEEP : ACTION_FORMAT;
        partition->target_fs = current == FS_VFAT ? FS_NONE : FS_VFAT;
    } else if (usage == PART_SWAP) {
        partition->action = current == FS_SWAP ? ACTION_KEEP : ACTION_FORMAT;
        partition->target_fs = current == FS_SWAP ? FS_NONE : FS_SWAP;
    } else if (filesystem_is_regular(current)) {
        partition->action = ACTION_KEEP;
        partition->target_fs = FS_NONE;
    } else {
        partition->action = ACTION_FORMAT;
        partition->target_fs = FS_EXT4;
    }
    if (!partition_supports_mount_profile(partition, partition->mount_profile)) {
        partition->mount_profile = MOUNT_PROFILE_DEFAULT;
    }
    return true;
}

void disk_plan_use_existing(DiskPlan *plan, const DiskInfo *disk)
{
    /*
     * 这里保存的是一次探测快照，而不只是界面上可见的文件系统名称。
     * 后续生成前会用起始扇区、容量和 GPT 身份重新确认它仍是同一分区。
     */
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
        target->mount_profile = MOUNT_PROFILE_DEFAULT;
    }
}

bool plan_add_disk(InstallPlan *plan, const DiskInfo *disk)
{
    DiskPlan *target;

    /* 重复添加视为成功，使调用方可以把“确保磁盘已加入”当作幂等操作。 */
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

/* 推荐 Swap 大小根据当前内存分段计算，读取失败时回退到 8 GiB。 */
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

/*
 * 引导式布局建立固定的分区角色和文件系统，并使各分区容量覆盖整块磁盘。
 * 实际写盘动作只会出现在之后生成的 Bash 脚本中。
 */
static void auto_partition(DiskPlan *storage, size_t index, unsigned number,
                           uint64_t bytes, PartitionUsage usage, Filesystem filesystem)
{
    PartitionPlan *partition = &storage->partitions[index];
    memset(partition, 0, sizeof(*partition));
    model_partition_device(partition->device, sizeof(partition->device), storage->path, number);
    partition->number = number;
    partition->size_bytes = bytes;
    partition->planned = true;
    partition->usage = usage;
    partition->action = ACTION_FORMAT;
    partition->target_fs = filesystem;
    partition->mount_profile = filesystem == FS_F2FS ?
                               MOUNT_PROFILE_BALANCED : MOUNT_PROFILE_DEFAULT;
}

void disk_plan_use_automatic(DiskPlan *storage, const DiskInfo *disk, StorageMode mode)
{
    uint64_t efi = GIB;
    uint64_t swap = recommended_swap_bytes();
    uint64_t remaining = disk->size_bytes > efi ? disk->size_bytes - efi : 0;

    storage->mode = mode;
    storage->partition_count = 0;
    if (mode == STORAGE_AUTO_DATA) {
        /* 数据盘先保持无挂载点；用户之后可选择挂载用途，也可只格式化。 */
        auto_partition(storage, storage->partition_count++, 1, disk->size_bytes,
                       PART_UNUSED, FS_EXT4);
        return;
    }

    auto_partition(storage, storage->partition_count++, 1, efi, PART_BOOT, FS_VFAT);

    if (mode == STORAGE_AUTO_ROOT_ONLY) {
        /* 模型记录完整剩余容量；生成 sfdisk 输入时才为 GPT 尾部留出余量。 */
        auto_partition(storage, storage->partition_count++, 2, remaining, PART_ROOT, FS_EXT4);
        return;
    }
    /* Swap 从可分配容量中先扣除，剩余空间再由 root/home 方案分配。 */
    if (remaining > swap) {
        remaining -= swap;
    } else {
        remaining = 0;
    }
    if (mode == STORAGE_AUTO_HOME_SWAP) {
        /* 此布局固定 root 为 100 GiB，home 吸收其余空间，便于校验器精确复核。 */
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

/* 枚举名称是界面显示和 Shell 序列化共同使用的稳定表示。 */
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

const char *mount_profile_name(MountProfile value)
{
    static const char *const names[] = {"default", "balanced", "compressed"};
    return value >= MOUNT_PROFILE_DEFAULT && value <= MOUNT_PROFILE_COMPRESSED ?
           names[value] : "default";
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

/*
 * 分区的“最终文件系统”由动作决定：FORMAT 使用目标格式，KEEP 使用探测到的
 * 现有格式。验证器、生成器和 TUI 必须共享这一规则，避免三处判断逐渐分叉。
 */
Filesystem partition_effective_filesystem(const PartitionPlan *partition)
{
    if (partition == NULL) return FS_NONE;
    return partition->action == ACTION_FORMAT ? partition->target_fs :
           filesystem_from_name(partition->current_fs);
}

/* 普通系统挂载点允许的文件系统集合集中维护，boot 和 swap 另有专用规则。 */
bool filesystem_is_regular(Filesystem filesystem)
{
    return filesystem == FS_EXT4 || filesystem == FS_XFS || filesystem == FS_F2FS;
}

/*
 * 挂载配置能力由模型统一判断，UI 和验证器无需分别了解各文件系统的支持表。
 * 当前所有可挂载文件系统都有 default，非默认配置暂时只开放给新建 F2FS。
 */
bool partition_supports_mount_profile(const PartitionPlan *partition,
                                      MountProfile profile)
{
    Filesystem filesystem;

    if (partition == NULL || profile < MOUNT_PROFILE_DEFAULT ||
        profile > MOUNT_PROFILE_COMPRESSED || partition->usage == PART_UNUSED ||
        partition->usage == PART_SWAP) return false;
    filesystem = partition_effective_filesystem(partition);
    if (filesystem == FS_NONE || filesystem == FS_SWAP) return false;
    if (profile == MOUNT_PROFILE_DEFAULT) return true;
    return filesystem == FS_F2FS && partition->action == ACTION_FORMAT;
}

/* UI 根据这一接口构造可选动作，验证器仍负责拒绝外部 JSON 中的非法组合。 */
bool partition_plan_action_allowed(const PartitionPlan *partition,
                                   PartitionAction action, Filesystem filesystem)
{
    Filesystem current;

    if (partition == NULL || partition->usage < PART_UNUSED ||
        partition->usage > PART_SWAP) return false;
    if (partition->planned &&
        (partition->usage == PART_BOOT || partition->usage == PART_SWAP)) return false;
    if (action == ACTION_KEEP) {
        if (filesystem != FS_NONE || partition->planned ||
            partition->usage == PART_ROOT || partition->usage == PART_VAR ||
            partition->usage == PART_USR) return false;
        current = filesystem_from_name(partition->current_fs);
        if (partition->usage == PART_BOOT) return current == FS_VFAT;
        if (partition->usage == PART_SWAP) return current == FS_SWAP;
        return filesystem_is_regular(current);
    }
    if (action != ACTION_FORMAT) return false;
    if (partition->usage == PART_BOOT) return filesystem == FS_VFAT;
    if (partition->usage == PART_SWAP) return filesystem == FS_SWAP;
    if (filesystem_is_regular(filesystem)) return true;
    return partition->usage == PART_UNUSED &&
           (filesystem == FS_VFAT || filesystem == FS_SWAP);
}

bool partition_plan_set_action(PartitionPlan *partition,
                               PartitionAction action, Filesystem filesystem)
{
    if (!partition_plan_action_allowed(partition, action, filesystem)) return false;
    partition->action = action;
    partition->target_fs = action == ACTION_KEEP ? FS_NONE : filesystem;
    if (!partition_supports_mount_profile(partition, partition->mount_profile)) {
        partition->mount_profile = MOUNT_PROFILE_DEFAULT;
    }
    return true;
}

bool partition_plan_set_mount_profile(PartitionPlan *partition, MountProfile profile)
{
    if (!partition_supports_mount_profile(partition, profile)) return false;
    partition->mount_profile = profile;
    return true;
}

/* 容量只负责适配人类可读显示，不参与方案中的精确字节计算。 */
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
