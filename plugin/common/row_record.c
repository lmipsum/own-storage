// SPDX-License-Identifier: GPL-3.0-or-later
#include "row_record.h"

#define FIELD_SEPARATOR '\t'
#define ESCAPE '\\'

/** The character an escape stands for: "\\", "\t", "\n"; any other is the character itself. */
static char unescaped(char code)
{
    return code == 't' ? '\t' : code == 'n' ? '\n' : code;
}

/**
 * One character of an escaped field at *at, unescaped; *at moves past it. A separator is never
 * taken as escaped, so that a field ends where row_record_fields ends it.
 */
static char next_char(const char **at, const char *end)
{
    const char c = *(*at)++;
    if (c == ESCAPE && *at < end && **at != FIELD_SEPARATOR)
    {
        return unescaped(*(*at)++);
    }
    return c;
}

int row_record_fields(const char *line, const char *end, const char **starts, const char **ends,
                      int room)
{
    int count = 0;
    const char *start = line;
    for (const char *at = line; count < room; ++at)
    {
        if (at == end || *at == FIELD_SEPARATOR)
        {
            starts[count] = start;
            ends[count] = at;
            ++count;
            if (at == end)
            {
                break;
            }
            start = at + 1;
        }
    }
    return count;
}

int row_field_equals(const char *from, const char *end, const char *plain)
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
        if (next_char(&at, end) != *plain++)
        {
            return 0;
        }
    }
}

int row_record_split(char *line, const char **fields, int room)
{
    int count = 0;
    char *write = line;
    const char *read = line;
    const char *end = line;
    while (*end != '\0')
    {
        ++end;
    }
    while (count < room)
    {
        fields[count++] = write;
        while (read < end && *read != FIELD_SEPARATOR)
        {
            *write++ = next_char(&read, end);
        }
        *write++ = '\0';
        if (read >= end)
        {
            break;
        }
        ++read;
    }
    return count;
}

int row_record_valid(char kind, int field_count)
{
    return (kind == ROW_RECORD_GAME && field_count >= ROW_GAME_FIELDS) ||
           (kind == ROW_RECORD_SAVE && field_count >= ROW_SAVE_FIELDS);
}
