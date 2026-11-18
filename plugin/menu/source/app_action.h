// SPDX-License-Identifier: GPL-3.0-or-later
// What the save menu asks the app for, as its xmbPlace's action file holds it, which the app's
// TakeXmbAction reads as it starts.

#ifndef OWNS_APP_ACTION_H
#define OWNS_APP_ACTION_H

// The actions, as the app's xmbDelete, xmbDownloadAll and xmbDeleteAll name them: a save's
// folder name or a game's ID follows.
#define APP_ACTION_DELETE "delete:"
#define APP_ACTION_DOWNLOAD_ALL "downloadall:"
#define APP_ACTION_DELETE_ALL "deleteall:"

/**
 * Leaves an action for the app: the action, then its argument.
 * @param argument a save folder's name or a game's ID: letters, digits, '-' and '_' only
 * @return 1 when it was written, and the app is to start
 */
int app_action_write(const char *action, const char *argument);

#endif
