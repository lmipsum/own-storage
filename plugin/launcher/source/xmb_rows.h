// SPDX-License-Identifier: GPL-3.0-or-later
// Which of the app's XMB save rows an item is. The app's owns_games.xml, as its XmbLists.cpp
// writes it, has a <Table key="owns_save_<n>"> a save, with <Pair key="title"> and
// <Pair key="info"> <String>s (XML-escaped, the info's lines split by a real line end); a save the
// rows act on has a script, xmb/owns_save_<n>.bat, whose "log save:<NAME>" line names its folder.

#ifndef OWNS_XMB_ROWS_H
#define OWNS_XMB_ROWS_H

#include <stddef.h>

// Room for a save row's key, owns_save_<n>.
#define SAVE_ROW_KEY_BYTES 32

/**
 * The key of the app's save row with this title and info line.
 * @param key set when exactly one row matches
 * @return how many rows match; -1 when the app's list cannot be read
 */
int xmb_rows_find_save(const char *title, const char *info, char *key, size_t key_room);

/**
 * The save folder a save row's script acts on.
 * @return 1 when the row has a script that names one
 */
int xmb_rows_save_name(const char *key, char *name, size_t name_room);

#endif
