#ifndef ARCH_INSTALLER_UTIL_COMPAT_H
#define ARCH_INSTALLER_UTIL_COMPAT_H

#include <stdbool.h>

#include "process.h"
#include "text.h"

/*
 * UI、packages 和 generator 主体将在后续阶段重构；该兼容头只汇总已拆分
 * 的入口，不对应 src/util.c，也不允许加入新的实现职责。
 */
bool valid_hostname(const char *value);
bool valid_username(const char *value);
bool valid_timezone(const char *value);
char *shell_quote(const char *value);

#endif
