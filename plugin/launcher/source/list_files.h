// SPDX-License-Identifier: GPL-3.0-or-later
// The app's list files, as an XMB list's URL names them. Each of the menu's, the saves' and the
// games' lists has a file of its own name, which holds the view the app last wrote, and a variant
// for each view of the same length, as the app's XmbMenuVariant, XmbSavesVariant and
// XmbGamesVariant name them: "owns_menu.xml" and "owns_m_<sort><group>.xml", "owns_saves.xml" and
// "owns_sa_<sort><group>.xml", "owns_games.xml" and "owns_ga_<sort>t.xml".

#ifndef OWNS_LIST_FILES_H
#define OWNS_LIST_FILES_H

#include <stddef.h>

#include "xmb_view.h"

/** Which of the app's lists a file is. */
enum list_file
{
    LIST_FILE_NONE,
    LIST_FILE_MENU,
    LIST_FILE_SAVES,
    LIST_FILE_GAMES,
};

// Room for a list file's name and its NUL.
#define LIST_FILE_NAME_BYTES 16

/**
 * The app's list file a URL names: the name after its last slash, before its '#'.
 * @param url the URL, NUL-ended
 * @param name_at set to where the name starts
 * @param name_length set to the name's bytes
 * @return LIST_FILE_NONE when it is none of the app's
 */
enum list_file list_file_in_url(const char *url, size_t *name_at, size_t *name_length);

/**
 * The view a list file holds, by its name.
 * @param name a name list_file_in_url found to be the app's
 * @return the variant's letters; XMB_VIEW_UNKNOWN letters for a file of the list's own name,
 *         which holds whatever view the app wrote last
 */
xmb_view list_file_view(const char *name, size_t length);

/**
 * The name of a list's file in a view; the games' lists only exist by title, whose group is
 * always theirs.
 * @param name receives it, NUL-ended; LIST_FILE_NAME_BYTES
 * @return its bytes; 0 for LIST_FILE_NONE
 */
size_t list_file_variant(enum list_file file, xmb_view view, char *name);

#endif
