#include "../private.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * PartitionRef 把跨磁盘分区展平成一张表；字段枚举让同一套数组输出器
 * 能生成设备、用途、动作、文件系统以及运行时身份校验所需的并行数组。
 */
typedef struct {
    const DiskPlan *disk;
    const PartitionPlan *partition;
    size_t disk_index;
} PartitionRef;

typedef enum {
    FIELD_DEVICE,
    FIELD_USAGE,
    FIELD_ACTION,
    FIELD_FILESYSTEM,
    FIELD_MOUNT_PROFILE,
    FIELD_MOUNTPOINT,
    FIELD_FS_UUID,
    FIELD_PART_UUID,
    FIELD_PART_TYPE
} PartitionField;

typedef enum {
    NUMBER_PARTITION,
    NUMBER_START_SECTOR,
    NUMBER_SIZE_BYTES
} PartitionNumberField;


const PartitionPlan *find_partition(const InstallPlan *plan, PartitionUsage usage)
{
    for (size_t disk_index = 0; disk_index < plan->storage.disk_count; ++disk_index) {
        const DiskPlan *disk = &plan->storage.disks[disk_index];
        for (size_t index = 0; index < disk->partition_count; ++index) {
            if (disk->partitions[index].usage == usage) {
                return &disk->partitions[index];
            }
        }
    }
    return NULL;
}

/* 根据分区对象反查所属磁盘，用于确定主要目标盘。 */
const DiskPlan *find_partition_disk(const InstallPlan *plan,
                                           const PartitionPlan *partition)
{
    if (partition == NULL) return NULL;
    for (size_t disk_index = 0; disk_index < plan->storage.disk_count; ++disk_index) {
        const DiskPlan *disk = &plan->storage.disks[disk_index];
        if (partition >= disk->partitions &&
            partition < disk->partitions + disk->partition_count) return disk;
    }
    return NULL;
}

/*
 * 可执行分区按全局挂载顺序收集：根分区最先，普通挂载点按路径排序，
 * Swap 随后，不分配挂载点但需要格式化的分区最后。
 */
static int partition_order(const PartitionRef *left, const PartitionRef *right)
{
    const char *left_mount;
    const char *right_mount;
    const PartitionPlan *left_part = left->partition;
    const PartitionPlan *right_part = right->partition;

    if (left_part->usage == PART_ROOT && right_part->usage != PART_ROOT) {
        return -1;
    }
    if (right_part->usage == PART_ROOT && left_part->usage != PART_ROOT) {
        return 1;
    }
    if (left_part->usage == PART_UNUSED && right_part->usage != PART_UNUSED) {
        return 1;
    }
    if (right_part->usage == PART_UNUSED && left_part->usage != PART_UNUSED) {
        return -1;
    }
    if (left_part->usage == PART_SWAP && right_part->usage != PART_SWAP) {
        return 1;
    }
    if (right_part->usage == PART_SWAP && left_part->usage != PART_SWAP) {
        return -1;
    }
    /* root 已被单独置顶，其余普通挂载点按路径字典序获得确定的处理顺序。 */
    left_mount = partition_mountpoint(left_part->usage);
    right_mount = partition_mountpoint(right_part->usage);
    return strcmp(left_mount, right_mount);
}

static size_t collect_actionable_partitions(const InstallPlan *plan,
                                            PartitionRef *partitions)
{
    size_t count = 0;

    for (size_t disk_index = 0; disk_index < plan->storage.disk_count; ++disk_index) {
        const DiskPlan *disk = &plan->storage.disks[disk_index];
        for (size_t index = 0; index < disk->partition_count; ++index) {
            const PartitionPlan *partition = &disk->partitions[index];
            PartitionRef entry;
            if (partition->usage == PART_UNUSED && partition->action != ACTION_FORMAT &&
                !partition->planned) continue;
            entry.disk = disk;
            entry.partition = partition;
            entry.disk_index = disk_index;
            size_t position = count;

            /*
             * 分区上限很小，边收集边插入排序比额外分配和 qsort 回调更直观；
             * 比较结果相等时不移动，磁盘内原有顺序也会保留下来。
             */
            while (position > 0 &&
                   partition_order(&entry, &partitions[position - 1]) < 0) {
                partitions[position] = partitions[position - 1];
                --position;
            }
            partitions[position] = entry;
            ++count;
        }
    }
    return count;
}

/* 分区的字符串字段和数值字段分别序列化为等长 Bash 数组。 */
static const char *partition_field(const PartitionPlan *partition, PartitionField field)
{
    Filesystem filesystem;

    switch (field) {
    case FIELD_DEVICE:
        return partition->device;
    case FIELD_USAGE:
        return usage_name(partition->usage);
    case FIELD_ACTION:
        return action_name(partition->action);
    case FIELD_FILESYSTEM:
        /* KEEP 输出探测到的格式，FORMAT 输出目标格式，模板无需再次理解动作语义。 */
        filesystem = partition_effective_filesystem(partition);
        return filesystem_name(filesystem);
    case FIELD_MOUNT_PROFILE:
        return mount_profile_name(partition->mount_profile);
    case FIELD_MOUNTPOINT:
        return partition_mountpoint(partition->usage);
    case FIELD_FS_UUID:
        return partition->fs_uuid;
    case FIELD_PART_UUID:
        return partition->part_uuid;
    case FIELD_PART_TYPE:
        return partition->part_type;
    }
    return "";
}

static void emit_partition_number_array(ScriptWriter *writer, const char *name,
                                        const PartitionRef *partitions,
                                        size_t count, PartitionNumberField field)
{
    writer_printf(writer, "%s=(\n", name);
    for (size_t index = 0; index < count; ++index) {
        uint64_t value;
        switch (field) {
        case NUMBER_PARTITION:
            value = partitions[index].partition->number;
            break;
        case NUMBER_START_SECTOR:
            value = partitions[index].partition->start_sector;
            break;
        case NUMBER_SIZE_BYTES:
            value = partitions[index].partition->size_bytes;
            break;
        default:
            value = 0;
            break;
        }
        writer_printf(writer, "    '%" PRIu64 "'\n", value);
    }
    writer_puts(writer, ")\n");
}

static void emit_partition_array(ScriptWriter *writer, const char *name,
                                 const PartitionRef *partitions,
                                 size_t count, PartitionField field)
{
    writer_printf(writer, "%s=(\n", name);
    for (size_t index = 0; index < count; ++index) {
        const char *value = partition_field(partitions[index].partition, field);
        char *quoted = shell_quote(value);

        if (quoted == NULL) {
            writer->ok = false;
            return;
        }
        writer_printf(writer, "    %s\n", quoted);
        free(quoted);
    }
    writer_puts(writer, ")\n");
}

static void emit_partition_disk_indexes(ScriptWriter *writer,
                                        const PartitionRef *partitions, size_t count)
{
    writer_puts(writer, "PART_DISK_INDEXES=(\n");
    for (size_t index = 0; index < count; ++index) {
        writer_printf(writer, "    '%zu'\n", partitions[index].disk_index);
    }
    writer_puts(writer, ")\n");
}

void emit_partition_plan(ScriptWriter *writer, const InstallPlan *plan)
{
    PartitionRef used[AI_MAX_PLAN_DISKS * AI_MAX_PARTITIONS];
    size_t used_count = collect_actionable_partitions(plan, used);

    /*
     * 以下 PART_* 数组全部由同一张已排序的 used 表生成。Shell 运行时以相同
     * 下标读取各项，新增字段时也必须保持这一对齐关系。
     */
    emit_partition_disk_indexes(writer, used, used_count);
    emit_partition_array(writer, "PART_DEVICES", used, used_count, FIELD_DEVICE);
    emit_partition_array(writer, "PART_USAGES", used, used_count, FIELD_USAGE);
    emit_partition_array(writer, "PART_ACTIONS", used, used_count, FIELD_ACTION);
    emit_partition_array(writer, "PART_FILESYSTEMS", used, used_count, FIELD_FILESYSTEM);
    emit_partition_array(writer, "PART_MOUNT_PROFILES", used, used_count,
                         FIELD_MOUNT_PROFILE);
    emit_partition_array(writer, "PART_MOUNTPOINTS", used, used_count, FIELD_MOUNTPOINT);
    emit_partition_array(writer, "PART_FS_UUIDS", used, used_count, FIELD_FS_UUID);
    emit_partition_array(writer, "PART_UUIDS", used, used_count, FIELD_PART_UUID);
    emit_partition_array(writer, "PART_TYPES", used, used_count, FIELD_PART_TYPE);
    emit_partition_number_array(writer, "PART_NUMBERS", used, used_count, NUMBER_PARTITION);
    emit_partition_number_array(writer, "PART_START_SECTORS", used, used_count,
                                NUMBER_START_SECTOR);
    emit_partition_number_array(writer, "PART_SIZES", used, used_count, NUMBER_SIZE_BYTES);
}
