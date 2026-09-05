#ifndef ARCH_INSTALLER_UI_H
#define ARCH_INSTALLER_UI_H

#include "model.h"
#include "packages.h"

int run_tui(InstallPlan *plan, HardwareInventory *inventory,
            const PackageConfig *packages, const char *plan_path,
            const char *script_path);

#endif
