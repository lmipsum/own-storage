// SPDX-License-Identifier: GPL-3.0-or-later
// paf's widgets, as the 4.82/4.88 vsh.self uses them: a PhWidget's vtable +0x11c is
// GetText(wstring &, part), which explore_plugin calls on XMB items with parts 4, 5 and 6. A
// PhXmList keeps its item count at +0x290 and its focused item at +0x29c.
#include "xmb_focus.h"

#include <stdint.h>

#include "paf.h"
#include "utf8.h"
#include "vsh_types.h"
#include "vtable.h"
#include "xmb_list.h"

#define WIDGET_GET_TEXT_SLOT 0x11c
#define XMLIST_ITEM_COUNT 0x290
#define XMLIST_FOCUSED_ITEM 0x29c

void *xmb_focused_item(void)
{
    void *list = xmb_focused_list();
    if (list == NULL)
    {
        return NULL;
    }
    const int32_t count = (int32_t)object_word(list, XMLIST_ITEM_COUNT);
    const int32_t index = (int32_t)object_word(list, XMLIST_FOCUSED_ITEM);
    // Read while explore_plugin may change the list: an index past its items is no item.
    return index >= 0 && index < count ? PhXmList_GetItemWidget(list, (int)index) : NULL;
}

size_t xmb_item_text(void *item, int part, char *text, size_t room)
{
    vsh_wstring units = {0, {{0}}, 0, VSH_WSTRING_IN_PLACE};
    typedef int (*get_text)(void *widget, vsh_wstring *text, int part);
    ((get_text)virtual_function(item, WIDGET_GET_TEXT_SLOT))(item, &units, part);
    const int on_heap = units.capacity > VSH_WSTRING_IN_PLACE;
    const unsigned short *unit = on_heap ? units.data.heap : units.data.text;
    size_t length = 0;
    // As the app writes its row index in UTF-8, a character beyond the first 65536 must come out
    // whole for the row to match.
    for (size_t i = 0; i < units.size && unit != NULL;)
    {
        length = utf8_put(text, length, room, utf16_next(unit, units.size, &i));
    }
    text[length] = '\0';
    // GetText filled a string of our own, which its destructor would free.
    if (on_heap && units.data.heap != NULL)
    {
        vsh_delete(units.data.heap);
    }
    return length;
}
