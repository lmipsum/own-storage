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
// The view of the saves in the cloud the user chose on the XMB, as the app's xmbViewFile names
// it; the app lists the saves so the next time it writes its lists.
#define OWNS_VIEW_FILE OWNS_APP_DIR "xmb_view.txt"
// The app's index of its XMB rows (xmbRowsFile), beside its list files.
#define OWNS_ROWS_FILE "/dev_hdd0/xmlhost/game_plugin/owns_rows.txt"
// The save folders the app downloaded, a full path and "\n" a line, as its saveRegisterRequest
// names it; the launcher registers them in the system's saved data list (save_register.h).
#define OWNS_REGISTER_REQUEST "/dev_hdd0/tmp/owns_register"

// The save menu's plugin view and its pages in owns_menu.rco (plugin/menu/owns_menu.json): the
// option menu, the full-screen information page with the plane its list is made on, the delete
// question's page, and the empty page a question's dialog opens on.
#define OWNS_MENU_VIEW "owns_menu"
#define OWNS_MENU_PAGE "owns_menu"
#define OWNS_INFO_PAGE "owns_info"
#define OWNS_INFO_PLANE "owns_info_plane"
// The full-screen page of a delete question, a copy of the Saved Data Utility's.
#define OWNS_DELETE_PAGE "owns_delete"
#define OWNS_DIALOG_PAGE "owns_dialog"

// The row the menu acts on, which the launcher writes before it loads the view (menu_target.h).
#define OWNS_MENU_TARGET "/dev_hdd0/tmp/owns_menu_target"
// The view asks the launcher with it to unload the view, once the menu has closed.
#define OWNS_MENU_RELEASE "/dev_hdd0/tmp/owns_menu_release"
// What the XMB had the input on before the menu took it (xmb_state.h).
#define OWNS_XMB_STATE "/dev_hdd0/tmp/owns_xmb_state"
// The open question's dialog, as hex, while it waits for an answer; recovery takes it away.
#define OWNS_DIALOG_OPEN "/dev_hdd0/tmp/owns_dialog_open"
// Recovery: when this file is there, the launcher closes whatever the menu has open and gives
// the input back to the XMB.
#define OWNS_MENU_REFOCUS "/dev_hdd0/tmp/owns_menu_refocus"

#endif
