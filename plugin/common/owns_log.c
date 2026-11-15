// SPDX-License-Identifier: GPL-3.0-or-later
// Built only with OWNS_PLUGIN_LOG; see owns_log.h.
#include "owns_log.h"

#include <cell/fs/cell_fs_file_api.h>
#include <stddef.h>

// A line's bytes; a longer text is cut, so that the value and the line end still fit.
#define LINE_BYTES 160
#define VALUE_BYTES 10

/** Appends a line to the log file: the text, then the value in hex when with_value is set. */
static void append_line(const char *text, uint32_t value, int with_value)
{
    char line[LINE_BYTES];
    size_t length = 0;
    for (; text[length] != '\0' && length < sizeof line - VALUE_BYTES - 1; ++length)
    {
        line[length] = text[length];
    }
    if (with_value)
    {
        line[length++] = ' ';
        for (int shift = 28; shift >= 0; shift -= 4)
        {
            line[length++] = "0123456789abcdef"[(value >> shift) & 0xF];
        }
    }
    line[length++] = '\n';
    int file;
    if (cellFsOpen(OWNS_LOG_FILE, CELL_FS_O_WRONLY | CELL_FS_O_CREAT | CELL_FS_O_APPEND, &file,
                   NULL, 0) == 0)
    {
        uint64_t written = 0;
        cellFsWrite(file, line, length, &written);
        cellFsClose(file);
    }
}

void owns_log(const char *text)
{
    append_line(text, 0, 0);
}

void owns_log_value(const char *text, uint32_t value)
{
    append_line(text, value, 1);
}
