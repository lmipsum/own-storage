// SPDX-License-Identifier: GPL-3.0-or-later
// A record of the app's row index (OWNS_ROWS_FILE), as its xmbRowsFile describes it: a line per
// XMB row, its fields separated by tabs, a backslash, a tab and a line break in a field written as
// "\\", "\t" and "\n".
//   G, key, game ID, title, description, number of saves
//   S, key, save folder's name, title, description, subtitle, stored, size, detail, game ID
// The title and the description are the row's own, as the XMB shows them.

#ifndef OWNS_ROW_RECORD_H
#define OWNS_ROW_RECORD_H

#include <stddef.h>

// The first field: a game's folder, or a save.
#define ROW_RECORD_GAME 'G'
#define ROW_RECORD_SAVE 'S'

/** A record's fields, by their place; a game's and a save's share the first five places. */
enum row_field
{
    ROW_FIELD_KIND = 0,
    ROW_FIELD_KEY = 1,
    ROW_FIELD_TITLE = 3,
    ROW_FIELD_INFO = 4,
    ROW_FIELD_GAME_ID = 2,
    ROW_FIELD_COUNT = 5,
    ROW_FIELD_SAVE_NAME = 2,
    ROW_FIELD_SUBTITLE = 5,
    ROW_FIELD_UPDATED = 6,
    ROW_FIELD_SIZE = 7,
    ROW_FIELD_DETAIL = 8,
    ROW_FIELD_SAVE_GAME_ID = 9,
};

// A record's fields; a record with fewer is not one.
#define ROW_GAME_FIELDS 6
#define ROW_SAVE_FIELDS 10
#define ROW_FIELDS_MAX ROW_SAVE_FIELDS

/**
 * The fields of a record [line, end), escaped as in the file, without changing it.
 * @param starts receives each field's start
 * @param ends receives each field's end
 * @return the record's field count, at most room
 */
int row_record_fields(const char *line, const char *end, const char **starts, const char **ends,
                      int room);

/**
 * Whether an escaped field [from, end) is a plain text once unescaped. Carriage returns are left
 * out of both, as the XMB's item text and the app may end their lines differently.
 */
int row_field_equals(const char *from, const char *end, const char *plain);

/**
 * Splits a NUL-ended record into its fields in place, unescaped and each NUL-ended.
 * @param fields receives each field
 * @return the record's field count, at most room
 */
int row_record_split(char *line, const char **fields, int room);

/** Whether a record's kind and field count make it a game's or a save's. */
int row_record_valid(char kind, int field_count);

#endif
