#define _POSIX_C_SOURCE 200809L

#include "detector.h"

#include "process.h"
#include "text.h"

#include <json-c/json.h>

#include <stdio.h>
#include <string.h>

/* 集中处理 lsblk JSON 中缺失值和 null，简化后续字段映射。 */
static const char *object_string(struct json_object *object, const char *key)
{
    struct json_object *value = NULL;
    if (!json_object_object_get_ex(object, key, &value) || value == NULL ||
        json_object_is_type(value, json_type_null)) {
        return "";
    }
    return json_object_get_string(value);
}

static uint64_t object_uint64(struct json_object *object, const char *key)
{
    struct json_object *value = NULL;
    if (!json_object_object_get_ex(object, key, &value) || value == NULL) return 0;
    return json_object_get_uint64(value);
}

static bool object_bool(struct json_object *object, const char *key)
{
    struct json_object *value = NULL;
    if (!json_object_object_get_ex(object, key, &value) || value == NULL) return false;
    return json_object_get_boolean(value) != 0;
}

/* lsblk 可能返回单个字符串或数组，这里统一取得第一个有效挂载点。 */
static void first_mountpoint(struct json_object *object, char *destination, size_t size)
{
    struct json_object *mountpoints = NULL;
    destination[0] = '\0';
    if (!json_object_object_get_ex(object, "mountpoints", &mountpoints) || mountpoints == NULL) {
        return;
    }
    if (json_object_is_type(mountpoints, json_type_string)) {
        copy_text(destination, size, json_object_get_string(mountpoints));
        return;
    }
    if (!json_object_is_type(mountpoints, json_type_array)) return;
    for (size_t index = 0; index < json_object_array_length(mountpoints); ++index) {
        struct json_object *item = json_object_array_get_idx(mountpoints, index);
        if (item != NULL && json_object_is_type(item, json_type_string)) {
            const char *value = json_object_get_string(item);
            if (value != NULL && value[0] != '\0') {
                copy_text(destination, size, value);
                return;
            }
        }
    }
}

/* 递归展开磁盘下的分区，同时把挂载或叠加设备传播为整盘占用状态。 */
static void collect_partitions(struct json_object *children, DiskInfo *disk)
{
    if (children == NULL || !json_object_is_type(children, json_type_array)) return;
    for (size_t index = 0; index < json_object_array_length(children); ++index) {
        struct json_object *item = json_object_array_get_idx(children, index);
        struct json_object *nested = NULL;
        const char *type = object_string(item, "type");
        if (strcmp(type, "part") == 0 && disk->partition_count < AI_MAX_PARTITIONS) {
            PartitionInfo *part = &disk->partitions[disk->partition_count++];
            memset(part, 0, sizeof(*part));
            copy_text(part->path, sizeof(part->path), object_string(item, "path"));
            copy_text(part->current_fs, sizeof(part->current_fs), object_string(item, "fstype"));
            copy_text(part->fs_uuid, sizeof(part->fs_uuid), object_string(item, "uuid"));
            copy_text(part->part_uuid, sizeof(part->part_uuid), object_string(item, "partuuid"));
            copy_text(part->part_type, sizeof(part->part_type), object_string(item, "parttype"));
            part->size_bytes = object_uint64(item, "size");
            part->start_sector = object_uint64(item, "start");
            part->number = (unsigned)object_uint64(item, "partn");
        }
        {
            char mounted[AI_PATH_LEN];
            first_mountpoint(item, mounted, sizeof(mounted));
            /*
             * 非 part 子节点通常是 dm-crypt、LVM 等叠加层；即使自身没报告
             * 挂载点，也说明该物理盘正在被上层设备使用，整盘操作需给出警告。
             */
            if (mounted[0] != '\0' || (type[0] != '\0' && strcmp(type, "part") != 0)) {
                disk->in_use = true;
            }
        }
        if (json_object_object_get_ex(item, "children", &nested)) {
            /* lsblk 的树可能是 disk -> part -> crypt -> lvm，递归确保不漏深层占用。 */
            collect_partitions(nested, disk);
        }
    }
}

bool detect_hardware(HardwareInventory *inventory, char *error, size_t error_size)
{
    ProcessResult process = {0};
    struct json_object *root = NULL;
    struct json_object *devices = NULL;
    char *const arguments[] = {
        "/usr/bin/lsblk", "--json", "--bytes", "--paths",
        /* NAME 虽不存入模型，却是 lsblk 生成 children 树结构所必需的控制列。 */
        "--output", "NAME,PATH,TYPE,SIZE,FSTYPE,UUID,MOUNTPOINTS,MODEL,SERIAL,TRAN,RM,RO,PARTN,PTTYPE,PARTUUID,PARTTYPE,START",
        NULL
    };

    /* 仅采集建模和运行时复核需要的字段，标准错误不会混入 JSON。 */
    memset(inventory, 0, sizeof(*inventory));
    if (!run_capture_stdout(arguments[0], arguments, &process, error, error_size)) return false;
    if (process.status != 0) {
        (void)snprintf(error, error_size, "lsblk failed: %.180s", process.output);
        process_result_free(&process);
        return false;
    }
    root = json_tokener_parse(process.output);
    process_result_free(&process);
    if (root == NULL || !json_object_object_get_ex(root, "blockdevices", &devices) ||
        !json_object_is_type(devices, json_type_array)) {
        (void)snprintf(error, error_size, "lsblk returned invalid JSON");
        if (root != NULL) json_object_put(root);
        return false;
    }

    /* 顶层只接受物理磁盘，分区和其下设备交给递归收集器处理。 */
    for (size_t index = 0; index < json_object_array_length(devices); ++index) {
        struct json_object *item = json_object_array_get_idx(devices, index);
        struct json_object *children = NULL;
        DiskInfo *disk;
        const char *transport;
        if (strcmp(object_string(item, "type"), "disk") != 0) continue;
        if (inventory->disk_count >= AI_MAX_DISKS) break;
        disk = &inventory->disks[inventory->disk_count++];
        memset(disk, 0, sizeof(*disk));
        copy_text(disk->path, sizeof(disk->path), object_string(item, "path"));
        copy_text(disk->model, sizeof(disk->model), object_string(item, "model"));
        copy_text(disk->serial, sizeof(disk->serial), object_string(item, "serial"));
        copy_text(disk->partition_table, sizeof(disk->partition_table), object_string(item, "pttype"));
        disk->size_bytes = object_uint64(item, "size");
        transport = object_string(item, "tran");
        disk->removable = object_bool(item, "rm") || strcmp(transport, "usb") == 0;
        disk->read_only = object_bool(item, "ro");
        {
            char mounted[AI_PATH_LEN];
            first_mountpoint(item, mounted, sizeof(mounted));
            if (mounted[0] != '\0') disk->in_use = true;
        }
        if (json_object_object_get_ex(item, "children", &children)) {
            collect_partitions(children, disk);
        }
    }
    json_object_put(root);
    if (inventory->disk_count == 0) {
        (void)snprintf(error, error_size, "no block devices of type 'disk' were found");
        return false;
    }
    return true;
}

const DiskInfo *inventory_find_disk(const HardwareInventory *inventory, const char *path)
{
    for (size_t index = 0; index < inventory->disk_count; ++index) {
        if (strcmp(inventory->disks[index].path, path) == 0) return &inventory->disks[index];
    }
    return NULL;
}
