// SPDX-License-Identifier: GPL-3.0-or-later
#include "utf8.h"

#define REPLACEMENT 0xFFFDu
#define SURROGATE_HIGH 0xD800u
#define SURROGATE_LOW 0xDC00u
#define SURROGATE_END 0xE000u
#define PLANE_1 0x10000u
#define CODE_MAX 0x10FFFFu

size_t utf8_put(char *text, size_t length, size_t room, unsigned int code)
{
    const size_t bytes = code < 0x80 ? 1 : code < 0x800 ? 2 : code < PLANE_1 ? 3 : 4;
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

unsigned int utf8_next(const char *text, size_t length, size_t *index)
{
    const unsigned char lead = (unsigned char)text[*index];
    ++*index;
    // The continuation bytes a lead byte announces, and the bits it carries itself.
    // 0x80..0xC1 only continue a character or start an overlong one; 0xF5 and above start none.
    size_t more = 4;
    if (lead < 0x80)
    {
        more = 0;
    }
    else if (lead >= 0xC2 && lead < 0xF5)
    {
        more = lead < 0xE0 ? 1 : lead < 0xF0 ? 2 : 3;
    }
    if (more == 0)
    {
        return lead;
    }
    if (more > 3 || *index + more > length)
    {
        return REPLACEMENT;
    }
    unsigned int code = lead & (0x3Fu >> more);
    for (size_t i = 0; i < more; ++i)
    {
        const unsigned char next = (unsigned char)text[*index + i];
        if ((next & 0xC0) != 0x80)
        {
            return REPLACEMENT;
        }
        code = (code << 6) | (next & 0x3Fu);
    }
    *index += more;
    // An overlong form, a surrogate or a code beyond Unicode is no character.
    static const unsigned int kLeast[] = {0, 0x80, 0x800, PLANE_1};
    if (code < kLeast[more] || code > CODE_MAX || (code >= SURROGATE_HIGH && code < SURROGATE_END))
    {
        return REPLACEMENT;
    }
    return code;
}

size_t utf16_put(unsigned short *units, size_t length, size_t room, unsigned int code)
{
    const size_t count = code < PLANE_1 ? 1 : 2;
    if (length + count >= room)
    {
        return length;
    }
    if (count == 1)
    {
        units[length] = (unsigned short)code;
        return length + 1;
    }
    code -= PLANE_1;
    units[length] = (unsigned short)(SURROGATE_HIGH + (code >> 10));
    units[length + 1] = (unsigned short)(SURROGATE_LOW + (code & 0x3FF));
    return length + 2;
}

unsigned int utf16_next(const unsigned short *units, size_t count, size_t *index)
{
    unsigned int code = units[*index];
    ++*index;
    // A surrogate pair stands for a character beyond the first 65536.
    if (code >= SURROGATE_HIGH && code < SURROGATE_LOW && *index < count &&
        units[*index] >= SURROGATE_LOW && units[*index] < SURROGATE_END)
    {
        code = PLANE_1 + ((code - SURROGATE_HIGH) << 10) + (units[*index] - SURROGATE_LOW);
        ++*index;
    }
    return code;
}
