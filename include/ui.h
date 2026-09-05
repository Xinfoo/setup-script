#ifndef ARCH_INSTALLER_UI_H
#define ARCH_INSTALLER_UI_H

#include "model.h"

int run_tui(InstallPlan *plan, HardwareInventory *inventory,
            const char *plan_path, const char *script_path);

#endif
