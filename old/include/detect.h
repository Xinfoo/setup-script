#ifndef ARCH_INSTALLER_DETECT_H
#define ARCH_INSTALLER_DETECT_H

#include <stddef.h>

#include "model.h"

/* 从当前系统探测磁盘及分区，并按设备路径查询探测结果。 */
bool detect_hardware(HardwareInventory *inventory, char *error, size_t error_size);
const DiskInfo *inventory_find_disk(const HardwareInventory *inventory, const char *path);

#endif
