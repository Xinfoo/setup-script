#ifndef ARCH_INSTALLER_UI_H
#define ARCH_INSTALLER_UI_H

#include "model.h"
#include "packages.h"

/* 启动交互式构造器；方案与硬件清单由调用方持有，TUI 只在会话期间引用。 */
int run_tui(InstallPlan *plan, HardwareInventory *inventory,
            const PackageConfig *packages, const char *plan_path,
            const char *script_path);

#endif
