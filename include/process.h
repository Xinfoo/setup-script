#ifndef ARCH_INSTALLER_PROCESS_H
#define ARCH_INSTALLER_PROCESS_H

#include <stdbool.h>
#include <stddef.h>

/* output 由运行函数分配，调用方必须使用 process_result_free 释放。 */
typedef struct {
    int status;
    char *output;
} ProcessResult;

/* 直接执行指定程序，捕获标准输出并丢弃标准错误。 */
bool run_capture_stdout(const char *program, char *const argv[], ProcessResult *result,
                        char *error, size_t error_size);
void process_result_free(ProcessResult *result);

#endif
