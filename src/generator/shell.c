#include "util.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

char *shell_quote(const char *value)
{
    size_t length = 3;
    char *quoted;
    char *cursor;

    if (value == NULL) {
        value = "";
    }
    /* 预先计算单引号转义后的精确长度，随后构造可直接嵌入 Bash 的字面量。 */
    for (const char *p = value; *p != '\0'; ++p) {
        if (*p == '\'') {
            if (length > SIZE_MAX - 4) {
                return NULL;
            }
            length += 4;
        } else {
            if (length == SIZE_MAX) {
                return NULL;
            }
            ++length;
        }
    }
    quoted = malloc(length);
    if (quoted == NULL) {
        return NULL;
    }
    cursor = quoted;
    *cursor++ = '\'';
    for (const char *p = value; *p != '\0'; ++p) {
        if (*p == '\'') {
            memcpy(cursor, "'\\''", 4);
            cursor += 4;
        } else {
            *cursor++ = *p;
        }
    }
    *cursor++ = '\'';
    *cursor = '\0';
    return quoted;
}
