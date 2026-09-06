#ifndef ARCH_INSTALLER_STORAGE_INVENTORY_H
#define ARCH_INSTALLER_STORAGE_INVENTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 探测快照与安装模型共享的固定容量和文本边界。 */
#define AI_MAX_DISKS 32
#define AI_MAX_PARTITIONS 128
#define AI_PATH_LEN 256
#define AI_TEXT_LEN 128

/* 只描述当前系统看到的磁盘和分区，不表达任何安装意图。 */
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

#endif
