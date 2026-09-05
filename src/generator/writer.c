#define _POSIX_C_SOURCE 200809L

#include "private.h"

#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void writer_puts(ScriptWriter *writer, const char *text)
{
    if (writer->ok && fputs(text, writer->file) == EOF) {
        writer->ok = false;
    }
}

void writer_printf(ScriptWriter *writer, const char *format, ...)
{
    va_list arguments;

    if (!writer->ok) {
        return;
    }
    va_start(arguments, format);
    if (vfprintf(writer->file, format, arguments) < 0) {
        writer->ok = false;
    }
    va_end(arguments);
}

bool emit_assignment(ScriptWriter *writer, const char *name, const char *value)
{
    char *quoted = shell_quote(value);

    if (quoted == NULL) {
        writer->ok = false;
        return false;
    }
    writer_printf(writer, "readonly %s=%s\n", name, quoted);
    free(quoted);
    return writer->ok;
}

void emit_boolean(ScriptWriter *writer, const char *name, bool value)
{
    writer_printf(writer, "readonly %s=%s\n", name, value ? "true" : "false");
}

void writer_write(ScriptWriter *writer, const void *data, size_t size)
{
    if (writer->ok && fwrite(data, 1, size, writer->file) != size) {
        writer->ok = false;
    }
}
