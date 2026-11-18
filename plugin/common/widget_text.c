// SPDX-License-Identifier: GPL-3.0-or-later
#include "widget_text.h"

#include <stddef.h>
#include <stdint.h>

#include "owns_text.h"
#include "utf8.h"
#include "vsh_types.h"
#include "vtable.h"

#define SET_TEXT_SLOT 0x118

/** Sets the part from units that are NUL-ended at length. */
static void set_units(void *widget, int part, unsigned short *units, uint32_t length)
{
    // SetText copies the string, so a longer text may point at the caller's buffer as its heap.
    vsh_wstring text = {0, {{0}}, length, VSH_WSTRING_IN_PLACE};
    if (length <= VSH_WSTRING_IN_PLACE)
    {
        for (uint32_t i = 0; i <= length; ++i)
        {
            text.data.text[i] = units[i];
        }
    }
    else
    {
        text.data.heap = units;
        text.capacity = length;
    }
    typedef int (*set_text)(void *widget, const vsh_wstring *text, int part);
    ((set_text)virtual_function(widget, SET_TEXT_SLOT))(widget, &text, part);
}

void widget_set_text(void *widget, int part, const unsigned short *text)
{
    unsigned short units[WIDGET_TEXT_UNITS];
    uint32_t length = 0;
    for (; text[length] != 0 && length + 1 < WIDGET_TEXT_UNITS; ++length)
    {
        units[length] = text[length];
    }
    units[length] = 0;
    set_units(widget, part, units, length);
}

int widget_set_utf8(void *widget, int part, const char *text)
{
    const size_t bytes = owns_text_length(text);
    // A character takes at most as many UTF-16 units as it has UTF-8 bytes.
    const size_t room = bytes + 1;
    unsigned short *units = (unsigned short *)vsh_new((unsigned int)(room * sizeof *units));
    if (units == NULL)
    {
        return 0;
    }
    size_t length = 0;
    for (size_t i = 0; i < bytes;)
    {
        length = utf16_put(units, length, room, utf8_next(text, bytes, &i));
    }
    units[length] = 0;
    set_units(widget, part, units, (uint32_t)length);
    vsh_delete(units);
    return 1;
}
