#include "../private.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>

#define MIB UINT64_C(1048576)

/* 自动布局尺寸和磁盘身份信息按磁盘下标写入并行数组。 */
static const char *storage_mode_value(StorageMode mode)
{
    switch (mode) {
    case STORAGE_EXISTING:
        return "existing";
    case STORAGE_AUTO_ROOT_SWAP:
        return "auto-root-swap";
    case STORAGE_AUTO_HOME_SWAP:
        return "auto-home-swap";
    case STORAGE_AUTO_ROOT_ONLY:
        return "auto-root-only";
    case STORAGE_AUTO_DATA:
        return "auto-data";
    }
    return "invalid";
}

static uint64_t disk_partition_size_mib(const DiskPlan *disk, PartitionUsage usage)
{
    for (size_t index = 0; index < disk->partition_count; ++index) {
        if (disk->partitions[index].usage == usage) {
            return disk->partitions[index].size_bytes / MIB;
        }
    }
    return 0;
}

static uint64_t flexible_size_mib(const DiskPlan *disk, PartitionUsage usage)
{
    uint64_t size = disk_partition_size_mib(disk, usage);

    /*
     * 模型按完整磁盘容量记账；这里只有吸收尾部空间的分区少报 2 MiB，
     * 为首扇区对齐和备用 GPT 表留出位置，避免 sfdisk 因边界取整失败。
     */
    return size > UINT64_C(2) ? size - UINT64_C(2) : 0;
}

static void emit_disk_string_array(ScriptWriter *writer, const char *name,
                                   const InstallPlan *plan, unsigned field)
{
    /* field 是本文件内部的紧凑选择器：path/model/serial/pttype/mode 依次为 0..4。 */
    writer_printf(writer, "%s=(\n", name);
    for (size_t index = 0; index < plan->storage.disk_count; ++index) {
        const DiskPlan *disk = &plan->storage.disks[index];
        const char *value = field == 0 ? disk->path : field == 1 ? disk->model :
                            field == 2 ? disk->serial : field == 3 ? disk->partition_table :
                            storage_mode_value(disk->mode);
        char *quoted = shell_quote(value);
        if (quoted == NULL) { writer->ok = false; return; }
        writer_printf(writer, "    %s\n", quoted);
        free(quoted);
    }
    writer_puts(writer, ")\n");
}

static void emit_disk_number_array(ScriptWriter *writer, const char *name,
                                   const InstallPlan *plan, unsigned field)
{
    /* 数值选择器依次对应整盘字节数以及 EFI/root/home/swap 的 MiB 大小。 */
    writer_printf(writer, "%s=(\n", name);
    for (size_t index = 0; index < plan->storage.disk_count; ++index) {
        const DiskPlan *disk = &plan->storage.disks[index];
        uint64_t value;
        if (field == 0) value = disk->size_bytes;
        else if (field == 1) value = disk_partition_size_mib(disk, PART_BOOT);
        else if (field == 2) value = disk->mode == STORAGE_AUTO_ROOT_SWAP
                                      ? flexible_size_mib(disk, PART_ROOT)
                                      : disk_partition_size_mib(disk, PART_ROOT);
        else if (field == 3) value = disk->mode == STORAGE_AUTO_HOME_SWAP
                                      ? flexible_size_mib(disk, PART_HOME)
                                      : disk_partition_size_mib(disk, PART_HOME);
        else value = disk_partition_size_mib(disk, PART_SWAP);
        writer_printf(writer, "    '%" PRIu64 "'\n", value);
    }
    writer_puts(writer, ")\n");
}

void emit_disk_plan(ScriptWriter *writer, const InstallPlan *plan)
{
    emit_disk_string_array(writer, "INSTALL_DISKS", plan, 0);
    emit_disk_string_array(writer, "DISK_MODELS", plan, 1);
    emit_disk_string_array(writer, "DISK_SERIALS", plan, 2);
    emit_disk_string_array(writer, "DISK_PTTYPES", plan, 3);
    emit_disk_string_array(writer, "DISK_MODES", plan, 4);
    emit_disk_number_array(writer, "DISK_SIZES", plan, 0);
    emit_disk_number_array(writer, "DISK_EFI_SIZE_MIB", plan, 1);
    emit_disk_number_array(writer, "DISK_ROOT_SIZE_MIB", plan, 2);
    emit_disk_number_array(writer, "DISK_HOME_SIZE_MIB", plan, 3);
    emit_disk_number_array(writer, "DISK_SWAP_SIZE_MIB", plan, 4);
}
