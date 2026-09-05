#ifndef ARCH_INSTALLER_UTIL_H
#define ARCH_INSTALLER_UTIL_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int status;
    char *output;
} ProcessResult;

bool run_capture(const char *program, char *const argv[], ProcessResult *result,
                 char *error, size_t error_size);
bool run_capture_stdout(const char *program, char *const argv[], ProcessResult *result,
                        char *error, size_t error_size);
void process_result_free(ProcessResult *result);
void copy_text(char *destination, size_t size, const char *source);
bool valid_hostname(const char *value);
bool valid_username(const char *value);
bool valid_timezone(const char *value);
char *shell_quote(const char *value);

#endif
