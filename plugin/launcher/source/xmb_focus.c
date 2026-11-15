// SPDX-License-Identifier: GPL-3.0-or-later
// paf's widgets, as the 4.82 vsh.self uses them: a PhWidget's vtable +0 is IsInherit (a class
// name pointer, as WidgetType returns it), +0x11c GetText(wstring &, part), which explore_plugin
// calls on XMB items with parts 4, 5 and 6; +0xec is its children's count. A PhXmList keeps its
// item count at +0x290 and its focused item at +0x29c; FocusIn sets the byte at +0x3a1 and
// FocusOut clears it. explore_plugin keeps one PhXmList per opened folder level on its list page.
#include "xmb_focus.h"

#include <stdint.h>

#include "owns_log.h"
#include "paf.h"
#include "utf8.h"
#include "vsh_types.h"

// The widget tree's walk stops this deep and after this many widgets, so a tree it does not
// expect cannot keep it going.
#define WALK_MAX_DEPTH 8
#define WALK_MAX_WIDGETS 600
#define LISTS_MAX 8
#define WIDGET_CHILD_COUNT 0xec
#define WIDGET_IS_INHERIT_SLOT 0x0
#define WIDGET_GET_TEXT_SLOT 0x11c
#define XMLIST_ITEM_COUNT 0x290
#define XMLIST_FOCUSED_ITEM 0x29c
#define XMLIST_FOCUS_FLAG 0x3a1

/** A word of a paf object. */
static uint32_t object_word(const void *object, uint32_t offset)
{
    return *(const volatile uint32_t *)((uintptr_t)object + offset);
}

/** Whether a widget is of a paf class or derives from it, by its virtual IsInherit. */
static int widget_is(void *widget, const char *type)
{
    typedef int (*is_inherit)(void *widget, const char *type);
    const is_inherit is =
        *(const is_inherit *)(uintptr_t)(object_word(widget, 0) + WIDGET_IS_INHERIT_SLOT);
    return is(widget, type) != 0;
}

/**
 * The widgets of a class in a tree, depth first, within the walk's limits; a match's own children
 * are not searched.
 * @param found receives them
 * @param room found's places
 * @return how many were stored in found
 */
static int find_widgets_of(void *root, const char *type, void **found, int room)
{
    if (widget_is(root, type))
    {
        found[0] = root;
        return 1;
    }
    void *path[WALK_MAX_DEPTH];
    int next[WALK_MAX_DEPTH];
    int depth = 0;
    int seen = 0;
    int count = 0;
    path[0] = root;
    next[0] = 0;
    while (depth >= 0 && seen < WALK_MAX_WIDGETS && count < room)
    {
        void *widget = path[depth];
        const int children = (int)object_word(widget, WIDGET_CHILD_COUNT);
        if (next[depth] >= children || depth + 1 >= WALK_MAX_DEPTH)
        {
            --depth;
            continue;
        }
        void *child = PhWidget_GetChild(widget, next[depth]++);
        ++seen;
        if (child == NULL)
        {
            continue;
        }
        if (widget_is(child, type))
        {
            found[count++] = child;
            continue;
        }
        ++depth;
        path[depth] = child;
        next[depth] = 0;
    }
    return count;
}

void *xmb_focused_item(void)
{
    const int explore = View_Find("explore_plugin");
    void *root = explore != 0 ? View_PageRoot(explore, "expage_explore_list") : NULL;
    if (root == NULL)
    {
        owns_log("no explore list page");
        return NULL;
    }
    void *lists[LISTS_MAX];
    const int count = find_widgets_of(root, PhXmList_WidgetType(), lists, LISTS_MAX);
    // The level FocusIn flagged; without a flag, the last one found.
    int chosen = count - 1;
    for (int i = 0; i < count; ++i)
    {
        if (*(const volatile uint8_t *)((uintptr_t)lists[i] + XMLIST_FOCUS_FLAG) != 0)
        {
            chosen = i;
        }
    }
    owns_log_value("explore lists", (uint32_t)count);
    if (chosen < 0 || (int32_t)object_word(lists[chosen], XMLIST_ITEM_COUNT) <= 0)
    {
        return NULL;
    }
    const int index = (int)object_word(lists[chosen], XMLIST_FOCUSED_ITEM);
    return index >= 0 ? PhXmList_GetItemWidget(lists[chosen], index) : NULL;
}

size_t xmb_item_text(void *item, int part, char *text, size_t room)
{
    vsh_wstring units = {0, {{0}}, 0, VSH_WSTRING_IN_PLACE};
    typedef int (*get_text)(void *widget, vsh_wstring *text, int part);
    const get_text get =
        *(const get_text *)(uintptr_t)(object_word(item, 0) + WIDGET_GET_TEXT_SLOT);
    get(item, &units, part);
    const int on_heap = units.capacity > VSH_WSTRING_IN_PLACE;
    const unsigned short *unit = on_heap ? units.data.heap : units.data.text;
    size_t length = 0;
    // As the app writes the lists in UTF-8, a character beyond the first 65536 must come out whole
    // for the row to match.
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
