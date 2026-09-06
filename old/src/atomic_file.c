#define _POSIX_C_SOURCE 200809L

#include "atomic_file.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * 公共写入流程只在所有步骤成功后替换目标：验证目标类型、创建同目录临时
 * 文件、交给回调输出、刷新并同步、关闭文件，最后执行原子 rename。
 */
bool atomic_write_file(const char *path, mode_t mode, AtomicFileWriter writer,
                       void *context, const char *description,
                       char *error, size_t error_size)
{
    struct stat status;
    char *temporary = NULL;
    FILE *file = NULL;
    int descriptor = -1;
    int saved_errno = 0;
    bool result = false;
    size_t temporary_size;

    if (error != NULL && error_size > 0) error[0] = '\0';
    if (path == NULL || path[0] == '\0' || writer == NULL) {
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "%s path and writer are required",
                           description != NULL ? description : "output");
        }
        return false;
    }
    if (description == NULL || description[0] == '\0') description = "output";

    /* 只接受不存在的目标或真实普通文件，避免最终 rename 覆盖链接或特殊节点。 */
    if (lstat(path, &status) == 0) {
        if (!S_ISREG(status.st_mode)) {
            if (error != NULL && error_size > 0) {
                (void)snprintf(error, error_size,
                               "refusing to replace non-regular %s path: %s",
                               description, path);
            }
            return false;
        }
    } else if (errno != ENOENT) {
        saved_errno = errno;
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot inspect %s path %s: %s",
                           description, path, strerror(saved_errno));
        }
        return false;
    }

    /* 临时文件必须与目标同目录，跨文件系统时 rename 将不再具备原子性。 */
    if (strlen(path) > SIZE_MAX - 16) {
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "%s path is too long", description);
        }
        return false;
    }
    temporary_size = strlen(path) + 16;
    temporary = malloc(temporary_size);
    if (temporary == NULL) {
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "out of memory while saving %s", description);
        }
        return false;
    }
    (void)snprintf(temporary, temporary_size, "%s.tmp.XXXXXX", path);
    descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        saved_errno = errno;
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot create temporary %s near %s: %s",
                           description, path, strerror(saved_errno));
        }
        goto finish;
    }
    if (fchmod(descriptor, mode) != 0) {
        saved_errno = errno;
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot set %s permissions for %s: %s",
                           description, path, strerror(saved_errno));
        }
        goto finish;
    }
    file = fdopen(descriptor, "w");
    if (file == NULL) {
        saved_errno = errno;
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot open temporary %s for %s: %s",
                           description, path, strerror(saved_errno));
        }
        goto finish;
    }
    descriptor = -1; /* fdopen 后描述符的所有权属于 FILE。 */

    /* 回调失败、stdio 刷新失败和磁盘同步失败都视为一次未提交写入。 */
    errno = 0;
    if (!writer(file, context) || fflush(file) != 0 || fsync(fileno(file)) != 0) {
        saved_errno = errno == 0 ? EIO : errno;
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot write %s %s: %s",
                           description, path, strerror(saved_errno));
        }
        goto finish;
    }
    /* 先确认流已成功关闭，再让 rename 成为调用方可见的唯一提交点。 */
    if (fclose(file) != 0) {
        file = NULL;
        saved_errno = errno;
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot close %s %s: %s",
                           description, path, strerror(saved_errno));
        }
        goto finish;
    }
    file = NULL;
    if (rename(temporary, path) != 0) {
        saved_errno = errno;
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "cannot commit %s %s: %s",
                           description, path, strerror(saved_errno));
        }
        goto finish;
    }
    result = true;

finish:
    /* 任意失败路径都回收当前所有权下的句柄，并移除未提交的临时文件。 */
    if (file != NULL) (void)fclose(file);
    if (descriptor >= 0) (void)close(descriptor);
    if (!result && temporary != NULL) (void)unlink(temporary);
    free(temporary);
    return result;
}

typedef struct {
    const char *text;
    bool trailing_newline;
} AtomicText;

/* 把常见的完整字符串保存适配到通用回调接口，避免 JSON 保存端重复写循环。 */
static bool write_atomic_text(FILE *file, void *context)
{
    const AtomicText *text = context;

    if (fputs(text->text, file) == EOF) return false;
    return !text->trailing_newline || fputc('\n', file) != EOF;
}

bool atomic_write_text_file(const char *path, mode_t mode, const char *text,
                            bool trailing_newline, const char *description,
                            char *error, size_t error_size)
{
    AtomicText context = {text, trailing_newline};

    if (text == NULL) {
        if (error != NULL && error_size > 0) {
            (void)snprintf(error, error_size, "%s content is required",
                           description != NULL ? description : "output");
        }
        return false;
    }
    return atomic_write_file(path, mode, write_atomic_text, &context, description,
                             error, error_size);
}
