// SPDX-License-Identifier: GPL-3.0-or-later
// The XMB's rows start Own Storage through this VSH plugin, the way its own icon starts it: the
// system's game_ext_plugin launches the installed app, whatever disc or game is mounted.
//
// The module's only segment is read-only once loaded, as the PS3 refuses to load a segment that
// is both writable and executable; so nothing here writes to a global, and the thread runs until
// the console shuts down.

#include <cell/fs/cell_fs_file_api.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/prx_module.h>
#include <sys/systime.h>
#include <sys/thread.h>

/** paf's View_Find: a loaded plugin's view; 0 when the plugin is not loaded. */
extern int View_Find(const char *name);
/** paf's plugin_GetInterface: one of a plugin's interfaces; NULL when it has none of the id. */
extern void *plugin_GetInterface(int view, int id);

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

// The file a row's script writes to ask for the app, as the app's xmbPlace names it; the plugin
// removes it as it starts the app.
static const char kRequest[] = "/dev_hdd0/tmp/owns_launch";
static const char kAppFolder[] = "OWNS00001";
static const unsigned int kPollMicroseconds = 300000;
// The watching thread waits most of the time; a small stack holds what it needs.
static const s32 kThreadPriority = 3000;
static const u64 kThreadStackBytes = 0x4000;

/** Starts the app, once game_ext_plugin is loaded. */
static void on_game_ext_loaded(void)
{
    const int view = View_Find("game_ext_plugin");
    void **entries = view != 0 ? (void **)plugin_GetInterface(view, 1) : NULL;
    if (entries != NULL)
    {
        ((select_by_folder)entries[SELECT_BY_FOLDER])(kAppFolder, HDD_GAME);
    }
}

/** Has game_ext_plugin loaded, which then starts the app. */
static void start_app(void)
{
    // Loaded already, it would not call the handler, and the request is dropped; the XMB unloads
    // it once the app has started, so the next request starts the app again.
    if (View_Find("game_ext_plugin") != 0)
    {
        return;
    }
    const int view = View_Find("xmb_plugin");
    xmm0_interface *xmm0 =
        view != 0 ? (xmm0_interface *)plugin_GetInterface(view, XMM0_INTERFACE) : NULL;
    if (xmm0 != NULL)
    {
        xmm0->LoadPlugin3(GAME_EXT_PLUGIN, (void *)on_game_ext_loaded, 0);
    }
}

/** Waits for a row's request, and starts the app for each. */
static void watch(void *unused)
{
    (void)unused;
    // A request left from before the console started, as when the user used a row before the
    // restart that loads this plugin, would start the app unasked.
    cellFsUnlink(kRequest);
    for (;;)
    {
        CellFsStat status;
        if (cellFsStat(kRequest, &status) == 0)
        {
            cellFsUnlink(kRequest);
            start_app();
        }
        sysUsleep(kPollMicroseconds);
    }
}

int module_start(size_t args, void *argp)
{
    (void)args;
    (void)argp;
    sys_ppu_thread_t thread;
    sysThreadCreate(&thread, watch, NULL, kThreadPriority, kThreadStackBytes, 0,
                    (char *)"owns_launcher");
    return SYS_PRX_RESIDENT;
}

int module_stop(size_t args, void *argp)
{
    (void)args;
    (void)argp;
    return SYS_PRX_RESIDENT;
}
