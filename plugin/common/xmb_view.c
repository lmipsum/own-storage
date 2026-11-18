// SPDX-License-Identifier: GPL-3.0-or-later
#include "xmb_view.h"

#include "owns_paths.h"
#include "owns_text.h"

// The file's two letters, and room for a line end the app allows after them.
#define VIEW_BYTES 8

_Static_assert(sizeof XMB_VIEW_SORT_LETTERS - 1 == XMB_VIEW_SORTS, "a letter a sort");
_Static_assert(sizeof XMB_VIEW_GROUP_LETTERS - 1 == XMB_VIEW_GROUPS, "a letter a group");

/** Whether a letter is one of a NUL-ended set. */
static int letter_in(char letter, const char *letters)
{
    for (; *letters != '\0'; ++letters)
    {
        if (*letters == letter)
        {
            return 1;
        }
    }
    return 0;
}

int xmb_view_is_sort(char letter)
{
    return letter_in(letter, XMB_VIEW_SORT_LETTERS);
}

int xmb_view_is_group(char letter)
{
    return letter_in(letter, XMB_VIEW_GROUP_LETTERS);
}

xmb_view xmb_view_read(void)
{
    xmb_view view = {XMB_VIEW_SORT_LETTERS[0], XMB_VIEW_GROUP_LETTERS[0]};
    char text[VIEW_BYTES];
    if (owns_read_file(OWNS_VIEW_FILE, text, sizeof text) >= 2 && xmb_view_is_sort(text[0]) &&
        xmb_view_is_group(text[1]))
    {
        view.sort = text[0];
        view.group = text[1];
    }
    return view;
}

int xmb_view_write(xmb_view view)
{
    const char text[] = {view.sort, view.group, '\0'};
    return owns_write_file(OWNS_VIEW_FILE, text);
}
