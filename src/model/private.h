#ifndef ARCH_INSTALLER_MODEL_PRIVATE_H
#define ARCH_INSTALLER_MODEL_PRIVATE_H

#include <stddef.h>

void model_partition_device(char *output, size_t size,
                            const char *disk, unsigned number);

#endif
