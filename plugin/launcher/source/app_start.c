// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_start.h"

#include <stddef.h>

#include "owns_log.h"
#include "owns_paths.h"
#include "paf.h"

// xmb_plugin's interface that loads other plugins, and game_ext_plugin's id among them, as webMAN
// MOD's vsh/xmb_plugin.h describes them.
#define XMM0_INTERFACE 0x584d4d30
#define GAME_EXT_PLUGIN 0x16

/** The start of xmb_plugin's XMM0 interface. */
typedef struct
{
    int (*GetPluginIdByName)(const char *);
    char *(*GetPluginNameById)(int);
    int (*IsPluginViewAvailable)(int);
    /** Loads a plugin, then calls the handler once its view is up. */
    int (*LoadPlugin3)(int, void *, int);
} xmm0_interface;

/** game_ext_plugin's interface 1, entry 3: starts the installed game in a folder. */
typedef int (*select_by_folder)(const char *folder, int place);
#define SELECT_BY_FOLDER 3
// The folder is one in /dev_hdd0/game.
#define HDD_GAME 2

/** Starts the app, once game_ext_plugin is loaded. */
static void on_game_ext_loaded(void)
{
    const int view = View_Find("game_ext_plugin");
    void **entries = view != 0 ? (void **)plugin_GetInterface(view, 1) : NULL;
    if (entries == NULL)
    {
        owns_log("app not started: no game_ext_plugin interface");
        return;
    }
    owns_log_value("app started", (uint32_t)((select_by_folder)entries[SELECT_BY_FOLDER])(
                                      OWNS_APP_FOLDER, HDD_GAME));
}

void app_start(void)
{
    // Loaded already, it would not call the handler, and the request is dropped; the XMB unloads
    // it once the app has started, so the next request starts the app again.
    if (View_Find("game_ext_plugin") != 0)
    {
        owns_log("app not started: game_ext_plugin is loaded");
        return;
    }
    const int view = View_Find("xmb_plugin");
    xmm0_interface *xmm0 =
        view != 0 ? (xmm0_interface *)plugin_GetInterface(view, XMM0_INTERFACE) : NULL;
    if (xmm0 == NULL)
    {
        owns_log("app not started: no xmb_plugin interface");
        return;
    }
    owns_log_value("game_ext_plugin load asked",
                   (uint32_t)xmm0->LoadPlugin3(GAME_EXT_PLUGIN, (void *)on_game_ext_loaded, 0));
}
