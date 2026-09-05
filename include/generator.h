#ifndef ARCH_INSTALLER_GENERATOR_H
#define ARCH_INSTALLER_GENERATOR_H

#include <stdbool.h>
#include <stddef.h>

#include "model.h"

bool generate_install_script(const InstallPlan *plan, const char *path,
                             char *error, size_t error_size);

#endif
