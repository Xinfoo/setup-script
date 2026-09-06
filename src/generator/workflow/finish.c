#include "../private.h"

/* chroot 返回后的宿主侧操作保持独立，避免私钥进入目标 chroot。 */
static const unsigned char finish_secure_boot_template[] = {
#include "generated/generator/finish_secure_boot.inc"
};

static const unsigned char finish_firmware_template[] = {
#include "generated/generator/finish_firmware.inc"
};

static const unsigned char finish_main_template[] = {
#include "generated/generator/finish_main.inc"
};

/* 收尾模板依赖少量系统开关，随后定义 Secure Boot、固件和主执行流程。 */
void emit_outer_finish(ScriptWriter *writer, const InstallPlan *plan)
{
    emit_boolean(writer, "IS_LAPTOP", plan->system.laptop);
    emit_boolean(writer, "ENABLE_SECURE_BOOT", plan->system.secure_boot);
    emit_boolean(writer, "USE_CHINA_MIRRORS", plan->system.china_mirrors);
    writer_write(writer, finish_secure_boot_template, sizeof(finish_secure_boot_template));
    writer_write(writer, finish_firmware_template, sizeof(finish_firmware_template));
    writer_write(writer, finish_main_template, sizeof(finish_main_template));
}
