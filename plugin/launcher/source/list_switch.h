// SPDX-License-Identifier: GPL-3.0-or-later
// Switching the XMB list the user is on to the view the user chose, in place: explore_plugin
// rebuilds an open list from its stored query on reload_list, and keeps each list file it read by
// its name; so the file name in the query's URL is changed to the chosen view's file, which is as
// long, then reload_list is run.

#ifndef OWNS_LIST_SWITCH_H
#define OWNS_LIST_SWITCH_H

#include "list_files.h"

/**
 * Which of the app's lists the list the user is on was opened from, and the view it shows.
 * @param shown set to the view its file holds; XMB_VIEW_UNKNOWN letters when it does not tell
 * @return LIST_FILE_NONE when it is none of the app's, or explore_plugin is not as traced
 */
enum list_file list_switch_current(xmb_view *shown);

/**
 * Switches the list the user is on to the view kept in OWNS_VIEW_FILE. Nothing is written unless
 * the URL names one of the app's list files, and the view's file name is as long and another; a
 * game's folder is switched within the views by title, which it belongs to.
 * @return 1 when the list was switched and reloaded
 */
int list_switch_to_kept_view(void);

#endif
