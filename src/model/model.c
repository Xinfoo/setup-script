#define _POSIX_C_SOURCE 200809L

#include "model.h"

#include "private.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

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

/* 新方案的默认值集中在这里，JSON 加载也以这些默认值作为初始化基线。 */
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
        target->f2fs_mode = F2FS_DEFAULT;
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
