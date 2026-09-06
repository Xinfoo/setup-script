#include "model.h"
#include "text.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n",               \
                          __FILE__, __LINE__, #condition);                       \
            return 1;                                                           \
        }                                                                       \
    } while (0)

/* 构造一份稳定的探测快照，专门验证从 UI 下沉到模型层的存储操作。 */
static void make_inventory(HardwareInventory *inventory)
{
    DiskInfo *disk;
    PartitionInfo *partition;

    memset(inventory, 0, sizeof(*inventory));
    inventory->disk_count = 1;
    disk = &inventory->disks[0];
    copy_text(disk->path, sizeof(disk->path), "/dev/vda");
    copy_text(disk->model, sizeof(disk->model), "Test virtual disk");
    copy_text(disk->serial, sizeof(disk->serial), "VDA-TEST-001");
    copy_text(disk->partition_table, sizeof(disk->partition_table), "gpt");
    disk->size_bytes = UINT64_C(64) * UINT64_C(1024) * UINT64_C(1024) *
                       UINT64_C(1024);
    disk->partition_count = 1;

    partition = &disk->partitions[0];
    copy_text(partition->path, sizeof(partition->path), "/dev/vda1");
    copy_text(partition->current_fs, sizeof(partition->current_fs), "ext4");
    copy_text(partition->fs_uuid, sizeof(partition->fs_uuid), "fs-uuid-1");
    copy_text(partition->part_uuid, sizeof(partition->part_uuid), "part-uuid-1");
    copy_text(partition->part_type, sizeof(partition->part_type), "linux");
    partition->size_bytes = disk->size_bytes;
    partition->start_sector = UINT64_C(2048);
    partition->number = 1;
}

int main(void)
{
    HardwareInventory inventory;
    InstallPlan plan;
    DiskPlan *disk;
    PartitionPlan *partition;

    make_inventory(&inventory);
    plan_init(&plan);
    CHECK(plan_add_disk(&plan, &inventory.disks[0]));
    CHECK(plan_storage_matches_inventory(&plan, &inventory));

    disk = &plan.storage.disks[0];
    partition = &disk->partitions[0];
    CHECK(partition_plan_assign_usage(partition, disk->mode, PART_ROOT));
    CHECK(partition->usage == PART_ROOT);
    CHECK(partition->action == ACTION_FORMAT);
    CHECK(partition->target_fs == FS_EXT4);
    CHECK(!partition_plan_action_allowed(partition, ACTION_KEEP, FS_NONE));
    CHECK(partition_plan_action_allowed(partition, ACTION_FORMAT, FS_F2FS));
    CHECK(partition_plan_set_action(partition, ACTION_FORMAT, FS_F2FS));
    CHECK(partition_plan_set_mount_profile(partition, MOUNT_PROFILE_COMPRESSED));
    CHECK(partition->mount_profile == MOUNT_PROFILE_COMPRESSED);
    CHECK(plan_find_disk_for_usage(&plan, PART_ROOT) == disk);
    CHECK(plan_storage_matches_inventory(&plan, &inventory));

    inventory.disks[0].partitions[0].start_sector += UINT64_C(1);
    CHECK(!plan_storage_matches_inventory(&plan, &inventory));
    inventory.disks[0].partitions[0].start_sector -= UINT64_C(1);

    partition->planned = true;
    CHECK(!partition_plan_assign_usage(partition, STORAGE_AUTO_ROOT_ONLY, PART_HOME));
    CHECK(partition->usage == PART_ROOT);
    CHECK(partition_plan_assign_usage(partition, STORAGE_AUTO_DATA, PART_UNUSED));
    CHECK(partition->action == ACTION_FORMAT);
    CHECK(partition->target_fs == FS_F2FS);
    CHECK(partition->mount_profile == MOUNT_PROFILE_DEFAULT);

    CHECK(plan_remove_disk_at(&plan, 0));
    CHECK(plan.storage.disk_count == 0);
    CHECK(!plan_remove_disk_at(&plan, 0));
    return 0;
}
