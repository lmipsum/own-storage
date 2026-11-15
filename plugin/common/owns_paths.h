// SPDX-License-Identifier: GPL-3.0-or-later
// The names and files the launcher, the save menu's view and the app agree on.

#ifndef OWNS_PATHS_H
#define OWNS_PATHS_H

// The installed app, as game_ext_plugin starts it, and its own folder.
#define OWNS_APP_FOLDER "OWNS00001"
#define OWNS_APP_DIR "/dev_hdd0/game/" OWNS_APP_FOLDER "/USRDIR/"
// A row's script and the save menu leave what the app is to do in the action file, as the app's
// xmbPlace names it, then ask the launcher for the app with the launch request.
#define OWNS_ACTION_FILE OWNS_APP_DIR "xmb_action.txt"
#define OWNS_LAUNCH_REQUEST "/dev_hdd0/tmp/owns_launch"

// The save menu's plugin view, and its page in owns_menu.rco (plugin/menu/owns_menu.json).
#define OWNS_MENU_VIEW "owns_menu"
#define OWNS_MENU_PAGE "owns_menu"
// The save folder the menu acts on, which the launcher writes before it loads the view.
#define OWNS_MENU_TARGET "/dev_hdd0/tmp/owns_menu_target"
// The view asks the launcher with it to unload the view, once the menu has closed.
#define OWNS_MENU_RELEASE "/dev_hdd0/tmp/owns_menu_release"

/** The save menu's rows, in the order they show; X on the save's row downloads it. */
enum owns_menu_row
{
    OWNS_ROW_INFO,
    OWNS_ROW_DELETE,
    OWNS_MENU_ROWS
};

#endif
