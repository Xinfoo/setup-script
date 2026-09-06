#ifndef ARCH_INSTALLER_ATOMIC_FILE_H
#define ARCH_INSTALLER_ATOMIC_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

/*
 * 在目标同目录写入临时文件，成功同步后再原子替换目标。回调只负责内容，
 * 路径检查、权限、清理和提交由公共实现统一处理。回调返回 false 表示内容
 * 不完整，公共实现会保留原目标并删除临时文件。
 */
typedef bool (*AtomicFileWriter)(FILE *file, void *context);

bool atomic_write_file(const char *path, mode_t mode, AtomicFileWriter writer,
                       void *context, const char *description,
                       char *error, size_t error_size);

/* 纯文本便捷入口可选择是否补一个结尾换行，其余提交语义与回调入口一致。 */
bool atomic_write_text_file(const char *path, mode_t mode, const char *text,
                            bool trailing_newline, const char *description,
                            char *error, size_t error_size);

#endif
