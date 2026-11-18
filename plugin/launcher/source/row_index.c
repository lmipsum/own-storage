// SPDX-License-Identifier: GPL-3.0-or-later
#include "row_index.h"

#include <cell/fs/cell_fs_file_api.h>
#include <stddef.h>
#include <stdint.h>

#include "owns_log.h"
#include "owns_paths.h"
#include "owns_text.h"
#include "row_record.h"
#include "vsh_types.h"

// An index far bigger than any the app writes is not read.
#define INDEX_MAX_BYTES (1024 * 1024)

/** Whether a record [line, end) is a game's or a save's row with this title and description. */
static int record_matches(const char *line, const char *end, const char *title, const char *info)
{
    const char *starts[ROW_FIELDS_MAX];
    const char *ends[ROW_FIELDS_MAX];
    const int count = row_record_fields(line, end, starts, ends, ROW_FIELDS_MAX);
    const char kind = ends[ROW_FIELD_KIND] - starts[ROW_FIELD_KIND] == 1 ? *line : '\0';
    return row_record_valid(kind, count) &&
           row_field_equals(starts[ROW_FIELD_TITLE], ends[ROW_FIELD_TITLE], title) &&
           row_field_equals(starts[ROW_FIELD_INFO], ends[ROW_FIELD_INFO], info);
}

/** A record [line, end) as a NUL-ended copy on the VSH's heap; NULL without room. */
static char *copy_record(const char *line, const char *end)
{
    const size_t length = (size_t)(end - line);
    char *copy = (char *)vsh_new((unsigned int)length + 1);
    if (copy != NULL)
    {
        for (size_t i = 0; i < length; ++i)
        {
            copy[i] = line[i];
        }
        copy[length] = '\0';
    }
    return copy;
}

/** The one record of [index, index_end) that matches; NULL when none or more than one do. */
static char *find_record(const char *index, const char *index_end, const char *title,
                         const char *info)
{
    const char *found = NULL;
    const char *found_end = NULL;
    int matches = 0;
    for (const char *line = index; line < index_end;)
    {
        const char *end = line;
        while (end < index_end && *end != '\n')
        {
            ++end;
        }
        const char *next = end + 1;
        if (end > line && end[-1] == '\r')
        {
            --end;
        }
        if (record_matches(line, end, title, info) && ++matches == 1)
        {
            found = line;
            found_end = end;
        }
        line = next;
    }
    owns_log_value("rows matching", (uint32_t)matches);
    return matches == 1 ? copy_record(found, found_end) : NULL;
}

char *row_index_find(const char *title, const char *info)
{
    CellFsStat status;
    if (cellFsStat(OWNS_ROWS_FILE, &status) != 0 || status.st_size == 0 ||
        status.st_size > INDEX_MAX_BYTES)
    {
        owns_log("no row index");
        return NULL;
    }
    // The heap's, as the index is too big for the thread's stack; nothing here may be a global.
    char *index = (char *)vsh_new((unsigned int)status.st_size + 1);
    if (index == NULL)
    {
        return NULL;
    }
    const int64_t length = owns_read_file(OWNS_ROWS_FILE, index, status.st_size + 1);
    char *record = length > 0 ? find_record(index, index + length, title, info) : NULL;
    vsh_delete(index);
    return record;
}
