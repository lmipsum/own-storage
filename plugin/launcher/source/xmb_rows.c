// SPDX-License-Identifier: GPL-3.0-or-later
#include "xmb_rows.h"

#include <cell/fs/cell_fs_file_api.h>
#include <stdint.h>

#include "owns_paths.h"
#include "owns_text.h"
#include "vsh_types.h"

#define GAMES_LIST "/dev_hdd0/xmlhost/game_plugin/owns_games.xml"
#define SCRIPT_DIR OWNS_APP_DIR "xmb/"
#define SCRIPT_SUFFIX ".bat"
#define SAVE_KEY_PREFIX "owns_save_"
#define SAVE_ACTION "log save:"
// Lists far bigger than any the app writes are not read.
#define LIST_MAX_BYTES (1024 * 1024)
#define PATH_BYTES 128
#define SCRIPT_BYTES 1024
#define PAIR_HEAD_BYTES 48

/** Where a text starts within [from, end); NULL when it is not there. */
static const char *find_text(const char *from, const char *end, const char *text)
{
    const size_t length = owns_text_length(text);
    for (const char *at = from; at + length <= end; ++at)
    {
        size_t i = 0;
        while (i < length && at[i] == text[i])
        {
            ++i;
        }
        if (i == length)
        {
            return at;
        }
    }
    return NULL;
}

/** One character of an XML-escaped text at *at, unescaped; *at moves past it. */
static char unescape_one(const char **at, const char *end)
{
    // The entities the app's Escaped writes.
    static const struct
    {
        const char *entity;
        char character;
    } kEntities[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}};
    for (size_t e = 0; e < sizeof kEntities / sizeof kEntities[0]; ++e)
    {
        const size_t length = owns_text_length(kEntities[e].entity);
        if (*at + length <= end && find_text(*at, *at + length, kEntities[e].entity) == *at)
        {
            *at += length;
            return kEntities[e].character;
        }
    }
    return *(*at)++;
}

/**
 * Whether an XML-escaped text [from, end) unescapes to a plain one; carriage returns are skipped,
 * as the XMB's item text and the file may end their lines differently.
 */
static int escaped_equals(const char *from, const char *end, const char *plain)
{
    const char *at = from;
    for (;;)
    {
        while (at < end && *at == '\r')
        {
            ++at;
        }
        while (*plain == '\r')
        {
            ++plain;
        }
        if (at >= end || *plain == '\0')
        {
            return at >= end && *plain == '\0';
        }
        if (unescape_one(&at, end) != *plain++)
        {
            return 0;
        }
    }
}

/**
 * A table's pair value, as <Pair key="name"><String>value</String>.
 * @param value set to the value's start
 * @param value_end set to the value's end
 * @return 1 when the table has the pair
 */
static int pair_value(const char *table, const char *table_end, const char *name,
                      const char **value, const char **value_end)
{
    char head[PAIR_HEAD_BYTES];
    size_t length = owns_text_append(head, 0, sizeof head, "<Pair key=\"");
    length = owns_text_append(head, length, sizeof head, name);
    owns_text_append(head, length, sizeof head, "\"><String>");
    const char *at = find_text(table, table_end, head);
    if (at == NULL)
    {
        return 0;
    }
    *value = at + owns_text_length(head);
    *value_end = find_text(*value, table_end, "</String>");
    return *value_end != NULL;
}

/**
 * The key of the one save row of a title and info line in the app's list [list, list_end).
 * @return how many save rows match; the key is set for the first
 */
static int match_save_rows(const char *list, const char *list_end, const char *title,
                           const char *info, char *key, size_t key_room)
{
    static const char kTable[] = "<Table key=\"";
    int matches = 0;
    if (key_room > 0)
    {
        key[0] = '\0';
    }
    for (const char *table = find_text(list, list_end, kTable); table != NULL;
         table = find_text(table + 1, list_end, kTable))
    {
        const char *key_start = table + sizeof kTable - 1;
        const char *key_end = find_text(key_start, list_end, "\"");
        const char *table_end = find_text(key_start, list_end, "</Table>");
        if (key_end == NULL || table_end == NULL ||
            find_text(key_start, key_end, SAVE_KEY_PREFIX) != key_start)
        {
            continue;
        }
        const char *value;
        const char *value_end;
        if (!pair_value(table, table_end, "title", &value, &value_end) ||
            !escaped_equals(value, value_end, title) ||
            !pair_value(table, table_end, "info", &value, &value_end) ||
            !escaped_equals(value, value_end, info))
        {
            continue;
        }
        // A key without room is no match the caller could use.
        if ((size_t)(key_end - key_start) >= key_room)
        {
            continue;
        }
        if (++matches == 1)
        {
            size_t i = 0;
            for (; key_start + i < key_end; ++i)
            {
                key[i] = key_start[i];
            }
            key[i] = '\0';
        }
    }
    return matches;
}

int xmb_rows_find_save(const char *title, const char *info, char *key, size_t key_room)
{
    CellFsStat status;
    if (cellFsStat(GAMES_LIST, &status) != 0 || status.st_size == 0 ||
        status.st_size > LIST_MAX_BYTES)
    {
        return -1;
    }
    // The heap's, as a list is too big for the thread's stack; nothing here may be a global.
    char *list = (char *)vsh_new((unsigned int)status.st_size + 1);
    if (list == NULL)
    {
        return -1;
    }
    const int64_t length = owns_read_file(GAMES_LIST, list, status.st_size + 1);
    const int matches =
        length < 0 ? -1 : match_save_rows(list, list + length, title, info, key, key_room);
    vsh_delete(list);
    return matches;
}

int xmb_rows_save_name(const char *key, char *name, size_t name_room)
{
    char path[PATH_BYTES];
    size_t length = owns_text_append(path, 0, sizeof path, SCRIPT_DIR);
    length = owns_text_append(path, length, sizeof path, key);
    owns_text_append(path, length, sizeof path, SCRIPT_SUFFIX);
    char script[SCRIPT_BYTES];
    const int64_t read = owns_read_file(path, script, sizeof script);
    const char *action = read > 0 ? find_text(script, script + read, SAVE_ACTION) : NULL;
    if (action == NULL)
    {
        return 0;
    }
    action += owns_text_length(SAVE_ACTION);
    size_t i = 0;
    for (; action[i] != '\0' && action[i] != '\n' && action[i] != '\r' && i + 1 < name_room; ++i)
    {
        name[i] = action[i];
    }
    name[i] = '\0';
    return i > 0;
}
