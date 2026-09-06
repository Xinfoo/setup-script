#define _POSIX_C_SOURCE 200809L

#include "atomic_file.h"
#include "generator.h"

#include "private.h"

#include <stdio.h>

typedef struct {
    const InstallPlan *plan;
    const PackageConfig *packages;
} GeneratorContext;

/* 原子写入回调只负责按固定阶段拼装脚本，提交和失败清理由公共层完成。 */
static bool write_install_script(FILE *file, void *opaque)
{
    const GeneratorContext *context = opaque;
    ScriptWriter writer = {file, true};

    /* 动态方案、Live 函数、chroot 配置和宿主收尾共同组成最终脚本。 */
    (void)emit_header_and_plan(&writer, context->plan, context->packages);
    emit_outer_runtime(&writer);
    (void)emit_chroot_configuration(&writer, context->plan, context->packages);
    emit_outer_finish(&writer, context->plan);
    return writer.ok;
}

/*
 * 生成入口先验证完整方案和输出目标，再在目标目录创建临时文件。
 * 所有阶段写入成功并完成 fsync 后才用 rename 原子替换最终脚本。
 */
bool generate_install_script(const InstallPlan *plan, const PackageConfig *packages,
                             const char *path,
                             char *error, size_t error_size)
{
    ValidationReport report;
    GeneratorContext context;

    if (error != NULL && error_size > 0) {
        error[0] = '\0';
    }
    if (plan == NULL || packages == NULL || path == NULL || path[0] == '\0') {
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size,
                           "plan, package config, and output path are required");
        }
        return false;
    }
    validate_plan(plan, &report);
    if (report.error_count != 0) {
        const char *message = "installation plan is invalid";

        for (size_t index = 0; index < report.count; ++index) {
            if (report.issues[index].severity == ISSUE_ERROR) {
                message = report.issues[index].message;
                break;
            }
        }
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot generate script: %s", message);
        }
        return false;
    }

    context.plan = plan;
    context.packages = packages;
    return atomic_write_file(path, 0750, write_install_script, &context,
                             "output", error, error_size);
}
