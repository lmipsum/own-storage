// SPDX-License-Identifier: GPL-3.0-or-later
#include "notices.h"

#include <cell/fs/cell_fs_file_api.h>
#include <stddef.h>

#include "paf.h"
#include "utf8.h"

/** vshtask's notification, which shows a line of UTF-8 in the XMB's corner. */
int vshtask_notify(int arg, const char *message);

// How much of a notification and of a {plugin:label} name the stack holds; more is left out.
#define LINE_BYTES 512
#define NAME_BYTES 64

/**
 * Adds a plugin's text in the system's language to a UTF-8 text.
 * @param name "plugin:label", not NUL-ended
 * @param size the name's bytes
 * @return the text's new length
 */
static size_t put_system_text(char *text, size_t length, size_t room, const char *name, size_t size)
{
    char plugin[NAME_BYTES];
    char label[NAME_BYTES];
    size_t colon = 0;
    while (colon < size && name[colon] != ':')
    {
        ++colon;
    }
    if (colon >= NAME_BYTES || colon == size || size - colon > NAME_BYTES)
    {
        return length;
    }
    for (size_t i = 0; i < colon; ++i)
    {
        plugin[i] = name[i];
    }
    plugin[colon] = '\0';
    for (size_t i = colon + 1; i < size; ++i)
    {
        label[i - colon - 1] = name[i];
    }
    label[size - colon - 1] = '\0';
    const int view = View_Find(plugin);
    const unsigned short *units = view != 0 ? View_GetString(view, label) : NULL;
    size_t count = 0;
    while (units != NULL && units[count] != 0)
    {
        ++count;
    }
    for (size_t i = 0; i < count;)
    {
        const unsigned int code = utf16_next(units, count, &i);
        // The system's texts break their lines; a notification is one line.
        length = utf8_put(text, length, room, code == '\n' ? ' ' : code);
    }
    return length;
}

/** Shows one line of the app's as a notification, its {plugin:label} parts in system words. */
static void notify(const char *line, size_t size)
{
    char text[LINE_BYTES];
    size_t length = 0;
    for (size_t i = 0; i < size; ++i)
    {
        size_t end = i;
        if (line[i] == '{')
        {
            while (end < size && line[end] != '}')
            {
                ++end;
            }
        }
        if (line[i] == '{' && end < size)
        {
            length = put_system_text(text, length, sizeof text, line + i + 1, end - i - 1);
            i = end;
        }
        else if (length + 1 < sizeof text)
        {
            text[length++] = line[i];
        }
    }
    text[length] = '\0';
    if (length > 0)
    {
        vshtask_notify(0, text);
    }
}

void notices_take(char *notices, uint64_t *size)
{
    int file;
    if (cellFsOpen(NOTICES_FILE, CELL_FS_O_RDONLY, &file, NULL, 0) != 0)
    {
        return;
    }
    uint64_t read = 0;
    cellFsRead(file, notices + *size, NOTICES_BYTES - *size, &read);
    cellFsClose(file);
    cellFsUnlink(NOTICES_FILE);
    *size += read;
}

void notices_show(const char *notices, uint64_t size)
{
    size_t start = 0;
    for (size_t i = 0; i <= size; ++i)
    {
        if (i == size || notices[i] == '\n')
        {
            notify(notices + start, i - start);
            start = i + 1;
        }
    }
}
