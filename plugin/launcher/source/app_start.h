// SPDX-License-Identifier: GPL-3.0-or-later
// Starting Own Storage the way its own icon starts it: the system's game_ext_plugin launches the
// installed app, whatever disc or game is mounted.

#ifndef OWNS_APP_START_H
#define OWNS_APP_START_H

/** Has game_ext_plugin loaded, which then starts the app. */
void app_start(void);

#endif
