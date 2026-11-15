// SPDX-License-Identifier: GPL-3.0-or-later
#include "utf8.h"

size_t utf8_put(char *text, size_t length, size_t room, unsigned int code)
{
    const size_t bytes = code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
    if (length + bytes >= room)
    {
        return length;
    }
    if (bytes == 1)
    {
        text[length] = (char)code;
        return length + 1;
    }
    // The lead byte's marks for 2, 3 and 4 bytes, then six bits a continuation byte.
    static const unsigned char kLead[] = {0, 0, 0xC0, 0xE0, 0xF0};
    for (size_t i = bytes - 1; i > 0; --i)
    {
        text[length + i] = (char)(0x80 | (code & 0x3F));
        code >>= 6;
    }
    text[length] = (char)(kLead[bytes] | code);
    return length + bytes;
}

unsigned int utf16_next(const unsigned short *units, size_t count, size_t *index)
{
    unsigned int code = units[*index];
    ++*index;
    // A surrogate pair stands for a character beyond the first 65536.
    if (code >= 0xD800 && code < 0xDC00 && *index < count && units[*index] >= 0xDC00 &&
        units[*index] < 0xE000)
    {
        code = 0x10000 + ((code - 0xD800) << 10) + (units[*index] - 0xDC00);
        ++*index;
    }
    return code;
}
