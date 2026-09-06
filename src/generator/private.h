#ifndef ARCH_INSTALLER_GENERATOR_PRIVATE_H
#define ARCH_INSTALLER_GENERATOR_PRIVATE_H

#include "generator.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* 任意一次写入失败后 ok 会保持为 false，后续写入自动成为空操作。 */
typedef struct {
    FILE *file;
    bool ok;
} ScriptWriter;

/* Bash 字面量和底层顺序输出。 */
char *shell_quote(const char *value);
void writer_puts(ScriptWriter *writer, const char *text);
void writer_write(ScriptWriter *writer, const void *data, size_t size);
void writer_printf(ScriptWriter *writer, const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
bool emit_assignment(ScriptWriter *writer, const char *name, const char *value);
void emit_boolean(ScriptWriter *writer, const char *name, bool value);

/* 按模型领域输出脚本变量和并行数组。 */
const PartitionPlan *find_partition(const InstallPlan *plan, PartitionUsage usage);
const DiskPlan *find_partition_disk(const InstallPlan *plan,
                                    const PartitionPlan *partition);
void emit_disk_plan(ScriptWriter *writer, const InstallPlan *plan);
void emit_partition_plan(ScriptWriter *writer, const InstallPlan *plan);
void emit_package_array(ScriptWriter *writer, const char *name,
                        const PackageConfig *config, PackageGroup group);
void emit_live_package_plan(ScriptWriter *writer, const InstallPlan *plan,
                            const PackageConfig *packages);

/* 四个 workflow 入口只负责保持最终脚本的装配顺序。 */
bool emit_header_and_plan(ScriptWriter *writer, const InstallPlan *plan,
                          const PackageConfig *packages);
void emit_outer_runtime(ScriptWriter *writer);
bool emit_chroot_configuration(ScriptWriter *writer, const InstallPlan *plan,
                               const PackageConfig *packages);
void emit_outer_finish(ScriptWriter *writer, const InstallPlan *plan);

#endif
