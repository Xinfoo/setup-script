#ifndef ARCH_INSTALLER_GENERATOR_PRIVATE_H
#define ARCH_INSTALLER_GENERATOR_PRIVATE_H

#include "generator.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/*
 * 所有生成阶段共享的输出状态。任意一次写入失败后 ok 会保持为 false，
 * 后续阶段可以继续调用 writer_*，但不会再向文件写入内容。
 */
typedef struct {
    FILE *file;
    bool ok;
} ScriptWriter;

/* 底层输出与需要 Shell 转义的变量写入。 */
void writer_puts(ScriptWriter *writer, const char *text);
void writer_write(ScriptWriter *writer, const void *data, size_t size);
void writer_printf(ScriptWriter *writer, const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
bool emit_assignment(ScriptWriter *writer, const char *name, const char *value);
void emit_boolean(ScriptWriter *writer, const char *name, bool value);

/* 脚本各阶段的拼装入口，调用顺序由 generator.c 统一控制。 */
const PartitionPlan *find_partition(const InstallPlan *plan, PartitionUsage usage);
void emit_package_array(ScriptWriter *writer, const char *name,
                        const PackageConfig *config, PackageGroup group);
bool emit_header_and_plan(ScriptWriter *writer, const InstallPlan *plan,
                          const PackageConfig *packages);
void emit_outer_runtime(ScriptWriter *writer);
bool emit_chroot_configuration(ScriptWriter *writer, const InstallPlan *plan,
                               const PackageConfig *packages);
void emit_outer_finish(ScriptWriter *writer, const InstallPlan *plan);

#endif
