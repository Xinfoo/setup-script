#ifndef ARCH_INSTALLER_MODEL_PRIVATE_H
#define ARCH_INSTALLER_MODEL_PRIVATE_H

#include <stddef.h>

/* 同时供方案构造和验证使用的 Linux 分区设备名生成规则。 */
void model_partition_device(char *output, size_t size,
                            const char *disk, unsigned number);

#endif
