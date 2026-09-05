#define _POSIX_C_SOURCE 200809L

#include "generator.h"

#include "private.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * 生成入口先验证完整方案和输出目标，再在目标目录创建临时文件。
 * 所有阶段写入成功并完成 fsync 后才用 rename 原子替换最终脚本。
 */
bool generate_install_script(const InstallPlan *plan, const PackageConfig *packages,
                             const char *path,
                             char *error, size_t error_size)
{
    ValidationReport report;
    ScriptWriter writer;
    FILE *file = NULL;
    char *temporary = NULL;
    int descriptor = -1;
    int saved_errno;
    int path_result;
    struct stat path_status;

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

    /* 只允许不存在的路径或普通文件，避免 rename 覆盖特殊文件和符号链接。 */
    path_result = lstat(path, &path_status);
    if (path_result == 0) {
        if (S_ISREG(path_status.st_mode)) {
            /* A successful atomic rename below will replace this regular file. */
        } else {
            if (error != NULL && error_size > 0) {
                (void)snprintf(error, error_size,
                               "refusing to replace non-regular output path: %s", path);
            }
            return false;
        }
    } else if (errno != ENOENT) {
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot inspect %s: %s", path, strerror(errno));
        }
        return false;
    }

    /* 临时文件与最终文件位于同一目录，确保最后的 rename 保持原子性。 */
    temporary = malloc(strlen(path) + 16);
    if (temporary == NULL) {
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "out of memory while creating output path");
        }
        return false;
    }
    (void)snprintf(temporary, strlen(path) + 16, "%s.tmp.XXXXXX", path);
    descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        saved_errno = errno;
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot create output near %s: %s",
                           path, strerror(saved_errno));
        }
        free(temporary);
        return false;
    }
    if (fchmod(descriptor, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP) != 0) {
        saved_errno = errno;
        (void)close(descriptor);
        (void)unlink(temporary);
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot set output permissions: %s",
                           strerror(saved_errno));
        }
        free(temporary);
        return false;
    }
    file = fdopen(descriptor, "w");
    if (file == NULL) {
        saved_errno = errno;
        (void)close(descriptor);
        (void)unlink(temporary);
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot open temporary output: %s",
                           strerror(saved_errno));
        }
        free(temporary);
        return false;
    }
    writer.file = file;
    writer.ok = true;

    /* 动态方案、Live 函数、chroot 配置和宿主收尾共同组成最终脚本。 */
    (void)emit_header_and_plan(&writer, plan, packages);
    emit_outer_runtime(&writer);
    (void)emit_chroot_configuration(&writer, plan, packages);
    emit_outer_finish(&writer, plan);

    /* 任何阶段失败都删除临时文件，不让不完整脚本出现在目标路径。 */
    if (!writer.ok || fflush(file) != 0 || fsync(fileno(file)) != 0) {
        saved_errno = errno;
        (void)fclose(file);
        (void)unlink(temporary);
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot write %s: %s", path,
                           strerror(saved_errno == 0 ? EIO : saved_errno));
        }
        free(temporary);
        return false;
    }
    if (fclose(file) != 0) {
        saved_errno = errno;
        (void)unlink(temporary);
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot close %s: %s", path,
                           strerror(saved_errno));
        }
        free(temporary);
        return false;
    }
    if (rename(temporary, path) != 0) {
        saved_errno = errno;
        (void)unlink(temporary);
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot commit %s: %s", path,
                           strerror(saved_errno));
        }
        free(temporary);
        return false;
    }
    free(temporary);
    return true;
}
