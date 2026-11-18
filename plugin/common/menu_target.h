// SPDX-License-Identifier: GPL-3.0-or-later
// The row the save menu acts on, which the launcher leaves in OWNS_MENU_TARGET before it loads the
// menu's view: where the row's list is, the view that list shows (its sort's and group's letters,
// '-' when not known), a tab, then the row's record from the app's index as it is there
// (row_record.h). It stays while the menu or one of its pages is open.

#ifndef OWNS_MENU_TARGET_H
#define OWNS_MENU_TARGET_H

#include <stddef.h>

#include "row_record.h"
#include "xmb_view.h"

/** Where the row's list is, which decides what the list's view can change there. */
enum menu_place
{
    /** The saves in the cloud: their order and their folders per game. */
    MENU_PLACE_CLOUD = 'C',
    /** A game's folder of saves, one of the folders per game: grouping them all leaves it. */
    MENU_PLACE_GAME_FOLDER = 'F',
    /** Not known: only the order, kept for the app without changing the list. */
    MENU_PLACE_OTHER = '-',
};

/** What the menu's levels need of the target: where the row's list is, its view, the row's kind. */
typedef struct
{
    enum menu_place place;
    /** The view the list shows; XMB_VIEW_UNKNOWN letters when its file does not tell. */
    xmb_view shown;
    /** ROW_RECORD_GAME or ROW_RECORD_SAVE. */
    char kind;
} menu_head;

/** The target as read: its head, and its record's fields, unescaped. */
typedef struct
{
    /** The file's text on the VSH's heap, which the fields point into. */
    char *text;
    menu_head head;
    const char *fields[ROW_FIELDS_MAX];
    int field_count;
} menu_target;

/**
 * Leaves the target for the menu's view.
 * @param shown the view the row's list shows; XMB_VIEW_UNKNOWN letters when not known
 * @param record the row's record, NUL-ended, as the index has it
 * @return 1 when it was written
 */
int menu_target_write(enum menu_place place, xmb_view shown, const char *record);

/**
 * Reads the target's head, without its fields.
 * @return 1 when the target is there and its kind a game's or a save's
 */
int menu_target_peek(menu_head *head);

/** Whether the shown view is known. */
int menu_head_view_known(const menu_head *head);

/**
 * Reads the whole target; menu_target_free gives its text back.
 * @return 1 when the target is there and its record a game's or a save's
 */
int menu_target_read(menu_target *target);

/** Frees what menu_target_read took. */
void menu_target_free(menu_target *target);

/** One of the record's fields (enum row_field); "" when the record has none there. */
const char *menu_target_field(const menu_target *target, int field);

#endif
