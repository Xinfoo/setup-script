#ifndef ARCH_INSTALLER_DETECT_H
#define ARCH_INSTALLER_DETECT_H

#include <stddef.h>

#include "model.h"

bool detect_hardware(HardwareInventory *inventory, char *error, size_t error_size);
const DiskInfo *inventory_find_disk(const HardwareInventory *inventory, const char *path);

#endif
