// SPDX-License-Identifier: GPL-3.0-or-later
#include "owns_text.h"

#include <cell/fs/cell_fs_file_api.h>

size_t owns_text_length(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0')
    {
        ++length;
    }
    return length;
}

size_t owns_text_append(char *text, size_t length, size_t room, const char *part)
{
    for (size_t i = 0; part[i] != '\0' && length + 1 < room; ++i)
    {
        text[length++] = part[i];
    }
    text[length] = '\0';
    return length;
}

size_t owns_text_put_hex(char *text, size_t length, uint32_t value)
{
    for (int shift = (OWNS_HEX_DIGITS - 1) * 4; shift >= 0; shift -= 4)
    {
        text[length++] = "0123456789abcdef"[(value >> shift) & 0xF];
    }
    return length;
}

uint32_t owns_text_get_hex(const char *text, int *valid)
{
    uint32_t value = 0;
    for (int i = 0; i < OWNS_HEX_DIGITS; ++i)
    {
        const char digit = text[i];
        if (digit >= '0' && digit <= '9')
        {
            value = (value << 4) | (uint32_t)(digit - '0');
        }
        else if (digit >= 'a' && digit <= 'f')
        {
            value = (value << 4) | (uint32_t)(digit - 'a' + 10);
        }
        else
        {
            *valid = 0;
            return 0;
        }
    }
    return value;
}

int64_t owns_read_file(const char *path, char *buffer, uint64_t room)
{
    int file;
    if (cellFsOpen(path, CELL_FS_O_RDONLY, &file, NULL, 0) != 0)
    {
        return -1;
    }
    uint64_t read = 0;
    const int result = cellFsRead(file, buffer, room - 1, &read);
    cellFsClose(file);
    if (result != 0)
    {
        return -1;
    }
    buffer[read] = '\0';
    return (int64_t)read;
}

int owns_write_file(const char *path, const char *text)
{
    int file;
    if (cellFsOpen(path, CELL_FS_O_WRONLY | CELL_FS_O_CREAT | CELL_FS_O_TRUNC, &file, NULL, 0) != 0)
    {
        return 0;
    }
    uint64_t written = 0;
    const size_t length = owns_text_length(text);
    const int result = cellFsWrite(file, text, length, &written);
    cellFsClose(file);
    return result == 0 && written == length;
}

int owns_file_exists(const char *path)
{
    CellFsStat status;
    return cellFsStat(path, &status) == 0;
}
