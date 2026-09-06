#ifndef ARCH_INSTALLER_TEXT_H
#define ARCH_INSTALLER_TEXT_H

#include <stddef.h>

/* 将 source 有界复制到固定容量缓冲区；NULL source 被视为空字符串。 */
void copy_text(char *destination, size_t size, const char *source);

#endif
