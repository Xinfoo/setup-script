#ifndef ARCH_INSTALLER_DETECTOR_H
#define ARCH_INSTALLER_DETECTOR_H

#include <stdbool.h>
#include <stddef.h>

#include "storage_inventory.h"

/* 从当前系统只读探测磁盘及分区，并按设备路径查询探测快照。 */
bool detect_hardware(HardwareInventory *inventory, char *error, size_t error_size);
const DiskInfo *inventory_find_disk(const HardwareInventory *inventory, const char *path);

#endif
