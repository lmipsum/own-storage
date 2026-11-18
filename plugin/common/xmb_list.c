// SPDX-License-Identifier: GPL-3.0-or-later
// paf's widgets, as the 4.82/4.88 vsh.self uses them: a PhWidget's vtable +0 is IsInherit (a class
// name pointer, as WidgetType returns it); +0xec is its children's count. A PhXmList's FocusIn
// sets the byte at +0x3a1 and FocusOut clears it.
#include "xmb_list.h"

#include <stddef.h>
#include <stdint.h>

#include "owns_log.h"
#include "paf.h"
#include "vtable.h"

// The widget tree's walk stops this deep and after this many widgets, so a tree it does not
// expect cannot keep it going.
#define WALK_MAX_DEPTH 8
#define WALK_MAX_WIDGETS 600
#define WIDGET_CHILD_COUNT 0xec
#define WIDGET_IS_INHERIT_SLOT 0x0
#define XMLIST_FOCUS_FLAG 0x3a1

/** Whether a widget is of a paf class or derives from it, by its virtual IsInherit. */
static int widget_is(void *widget, const char *type)
{
    typedef int (*is_inherit)(void *widget, const char *type);
    return ((is_inherit)virtual_function(widget, WIDGET_IS_INHERIT_SLOT))(widget, type) != 0;
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

int xmb_lists(void **lists, int room)
{
    const int explore = View_Find("explore_plugin");
    void *root = explore != 0 ? View_PageRoot(explore, XMB_LIST_PAGE) : NULL;
    return root != NULL ? find_widgets_of(root, PhXmList_WidgetType(), lists, room) : 0;
}

void *xmb_focused_list(void)
{
    void *lists[XMB_LISTS_MAX];
    const int count = xmb_lists(lists, XMB_LISTS_MAX);
    owns_log_value("explore lists", (uint32_t)count);
    // The level FocusIn flagged; without a flag, the last one found.
    int chosen = count - 1;
    for (int i = 0; i < count; ++i)
    {
        if (*(const volatile uint8_t *)((uintptr_t)lists[i] + XMLIST_FOCUS_FLAG) != 0)
        {
            chosen = i;
        }
    }
    return chosen >= 0 ? lists[chosen] : NULL;
}
