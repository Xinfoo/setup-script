#include "private.h"

static const unsigned char finish_secure_boot_template[] = {
#include "generated/generator/finish_secure_boot.inc"
};

static const unsigned char finish_target_package_template[] = {
#include "generated/generator/finish_target_package.inc"
};

static const unsigned char finish_firmware_template[] = {
#include "generated/generator/finish_firmware.inc"
};

static const unsigned char finish_main_template[] = {
#include "generated/generator/finish_main.inc"
};

void emit_outer_finish(ScriptWriter *writer, const InstallPlan *plan)
{
    emit_boolean(writer, "IS_LAPTOP", plan->system.laptop);
    emit_boolean(writer, "ENABLE_SECURE_BOOT", plan->system.secure_boot);
    emit_boolean(writer, "USE_CHINA_MIRRORS", plan->system.china_mirrors);
    writer_write(writer, finish_secure_boot_template, sizeof(finish_secure_boot_template));
    writer_write(writer, finish_target_package_template, sizeof(finish_target_package_template));
    writer_write(writer, finish_firmware_template, sizeof(finish_firmware_template));
    writer_write(writer, finish_main_template, sizeof(finish_main_template));
}
