#include "model.h"

#include "text.h"

#include <string.h>

/* 新方案的默认值集中在这里，JSON 加载也以这些默认值作为初始化基线。 */
void plan_init(InstallPlan *plan)
{
    memset(plan, 0, sizeof(*plan));
    plan->version = AI_PLAN_VERSION;
    plan->system.platform = PLATFORM_INTEL;
    plan->system.kernel = KERNEL_LINUX;
    plan->system.locale = LOCALE_EN_US;
    plan->system.desktop = DESKTOP_KDE;
    copy_text(plan->system.timezone, sizeof(plan->system.timezone), "Asia/Shanghai");
    copy_text(plan->system.hostname, sizeof(plan->system.hostname), "ARCH-LINUX");
    copy_text(plan->system.username, sizeof(plan->system.username), "user");
    plan->system.desktop_recommended = true;
    plan->system.archive_tools = true;
    plan->system.terminal_tools = true;
    plan->system.china_mirrors = true;
    plan->system.create_efi_entry = true;
}
