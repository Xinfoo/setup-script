#include "private.h"

static const unsigned char runtime_state_template[] = {
#include "generated/generator/runtime_state.inc"
};

static const unsigned char runtime_cleanup_template[] = {
#include "generated/generator/runtime_cleanup.inc"
};

static const unsigned char runtime_logging_template[] = {
#include "generated/generator/runtime_logging.inc"
};

static const unsigned char runtime_local_mirror_template[] = {
#include "generated/generator/runtime_local_mirror.inc"
};

static const unsigned char runtime_secure_boot_input_template[] = {
#include "generated/generator/runtime_secure_boot_input.inc"
};

static const unsigned char runtime_storage_validation_template[] = {
#include "generated/generator/runtime_storage_validation.inc"
};

static const unsigned char runtime_keep_probe_template[] = {
#include "generated/generator/runtime_keep_probe.inc"
};

static const unsigned char runtime_preflight_template[] = {
#include "generated/generator/runtime_preflight.inc"
};

static const unsigned char runtime_plan_confirmation_template[] = {
#include "generated/generator/runtime_plan_confirmation.inc"
};

static const unsigned char runtime_partitioning_template[] = {
#include "generated/generator/runtime_partitioning.inc"
};

static const unsigned char runtime_filesystems_template[] = {
#include "generated/generator/runtime_filesystems.inc"
};

static const unsigned char runtime_target_mirror_template[] = {
#include "generated/generator/runtime_target_mirror.inc"
};

static const unsigned char runtime_package_source_template[] = {
#include "generated/generator/runtime_package_source.inc"
};

void emit_outer_runtime(ScriptWriter *writer)
{
    writer_write(writer, runtime_state_template, sizeof(runtime_state_template));
    writer_write(writer, runtime_cleanup_template, sizeof(runtime_cleanup_template));
    writer_write(writer, runtime_logging_template, sizeof(runtime_logging_template));
    writer_write(writer, runtime_local_mirror_template, sizeof(runtime_local_mirror_template));
    writer_write(writer, runtime_secure_boot_input_template,
                 sizeof(runtime_secure_boot_input_template));
    writer_write(writer, runtime_storage_validation_template,
                 sizeof(runtime_storage_validation_template));
    writer_write(writer, runtime_keep_probe_template, sizeof(runtime_keep_probe_template));
    writer_write(writer, runtime_preflight_template, sizeof(runtime_preflight_template));
    writer_write(writer, runtime_plan_confirmation_template,
                 sizeof(runtime_plan_confirmation_template));
    writer_write(writer, runtime_partitioning_template, sizeof(runtime_partitioning_template));
    writer_write(writer, runtime_filesystems_template, sizeof(runtime_filesystems_template));
    writer_write(writer, runtime_target_mirror_template, sizeof(runtime_target_mirror_template));
    writer_write(writer, runtime_package_source_template, sizeof(runtime_package_source_template));
}
