#ifndef ARCH_INSTALLER_UTIL_H
#define ARCH_INSTALLER_UTIL_H

#include <stdbool.h>
#include <stddef.h>

/* output 由运行函数分配，调用方必须使用 process_result_free 释放。 */
typedef struct {
    int status;
    char *output;
} ProcessResult;

/* 执行外部程序并捕获输出；两个入口分别合并或丢弃标准错误。 */
bool run_capture(const char *program, char *const argv[], ProcessResult *result,
                 char *error, size_t error_size);
bool run_capture_stdout(const char *program, char *const argv[], ProcessResult *result,
                        char *error, size_t error_size);
void process_result_free(ProcessResult *result);

/* 通用字符串复制与安装计划文本字段校验。 */
void copy_text(char *destination, size_t size, const char *source);
bool valid_hostname(const char *value);
bool valid_username(const char *value);
bool valid_timezone(const char *value);

/* 返回新分配的 Bash 单引号字面量，调用方负责 free。 */
char *shell_quote(const char *value);

#endif
