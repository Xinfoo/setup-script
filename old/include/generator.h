#ifndef ARCH_INSTALLER_GENERATOR_H
#define ARCH_INSTALLER_GENERATOR_H

#include <stdbool.h>
#include <stddef.h>

#include "model.h"
#include "packages.h"

/* 验证方案并原子生成可执行的 Bash 安装脚本；失败原因写入 error。 */
bool generate_install_script(const InstallPlan *plan, const PackageConfig *packages,
                             const char *path,
                             char *error, size_t error_size);

#endif
