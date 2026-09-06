#include "text.h"

#include <stdio.h>

/* 所有固定容量字符串都通过该入口进行有界复制，并统一处理空指针。 */
void copy_text(char *destination, size_t size, const char *source)
{
    if (size == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    (void)snprintf(destination, size, "%s", source);
}
