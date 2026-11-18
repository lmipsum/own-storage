// SPDX-License-Identifier: GPL-3.0-or-later
#include "list_files.h"

#include "owns_text.h"

#define LIST_SUFFIX ".xml"

/** A list's files: its own name, and the stem of its variants. */
typedef struct
{
    enum list_file file;
    const char *own_name;
    const char *variant_stem;
} list_names;

static const list_names kLists[] = {
    {LIST_FILE_MENU, "owns_menu.xml", "owns_m_"},
    {LIST_FILE_SAVES, "owns_saves.xml", "owns_sa_"},
    {LIST_FILE_GAMES, "owns_games.xml", "owns_ga_"},
};

#define LISTS (sizeof kLists / sizeof kLists[0])

/** Whether [text, text + length) is a NUL-ended text exactly. */
static int same_text(const char *text, size_t length, const char *expected)
{
    size_t i = 0;
    while (i < length && expected[i] != '\0' && text[i] == expected[i])
    {
        ++i;
    }
    return i == length && expected[i] == '\0';
}

/** Whether a name is one of a list's variants: its stem, a view's letters, the suffix. */
static int is_variant(const char *name, size_t length, const char *stem)
{
    const size_t stem_length = owns_text_length(stem);
    const size_t suffix_length = sizeof LIST_SUFFIX - 1;
    return length == stem_length + 2 + suffix_length && same_text(name, stem_length, stem) &&
           xmb_view_is_sort(name[stem_length]) && xmb_view_is_group(name[stem_length + 1]) &&
           same_text(name + stem_length + 2, suffix_length, LIST_SUFFIX);
}

enum list_file list_file_in_url(const char *url, size_t *name_at, size_t *name_length)
{
    size_t end = 0;
    while (url[end] != '\0' && url[end] != '#')
    {
        ++end;
    }
    size_t start = end;
    while (start > 0 && url[start - 1] != '/')
    {
        --start;
    }
    *name_at = start;
    *name_length = end - start;
    for (size_t i = 0; i < LISTS; ++i)
    {
        if (same_text(url + start, end - start, kLists[i].own_name) ||
            is_variant(url + start, end - start, kLists[i].variant_stem))
        {
            return kLists[i].file;
        }
    }
    return LIST_FILE_NONE;
}

xmb_view list_file_view(const char *name, size_t length)
{
    xmb_view view = {XMB_VIEW_UNKNOWN, XMB_VIEW_UNKNOWN};
    for (size_t i = 0; i < LISTS; ++i)
    {
        if (is_variant(name, length, kLists[i].variant_stem))
        {
            const size_t letters = owns_text_length(kLists[i].variant_stem);
            view.sort = name[letters];
            view.group = name[letters + 1];
        }
    }
    return view;
}

size_t list_file_variant(enum list_file file, xmb_view view, char *name)
{
    for (size_t i = 0; i < LISTS; ++i)
    {
        if (kLists[i].file == file)
        {
            const char group = file == LIST_FILE_GAMES ? XMB_VIEW_BY_TITLE : view.group;
            const char letters[] = {view.sort, group, '\0'};
            size_t length = owns_text_append(name, 0, LIST_FILE_NAME_BYTES, kLists[i].variant_stem);
            length = owns_text_append(name, length, LIST_FILE_NAME_BYTES, letters);
            return owns_text_append(name, length, LIST_FILE_NAME_BYTES, LIST_SUFFIX);
        }
    }
    name[0] = '\0';
    return 0;
}
