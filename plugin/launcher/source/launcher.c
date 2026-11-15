// SPDX-License-Identifier: GPL-3.0-or-later
// The XMB's rows start Own Storage through this VSH plugin, the way its own icon starts it. The
// app's results show through it too, as the XMB's notifications. And Triangle on one of the app's
// save rows opens the save menu, whose view (plugin/menu) the plugin loads and unloads.
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

#include "app_start.h"
#include "menu_view.h"
#include "notices.h"
#include "owns_log.h"
#include "owns_paths.h"
#include "pad.h"
#include "paf.h"
#include "save_menu.h"

// Triangle is polled about 30 times a second, so a short press is not missed; the request files
// are looked at every ninth poll, about every 0.3 s.
static const unsigned int kPadPollMicroseconds = 33333;
static const int kPadPollsPerRound = 9;
// The watching thread waits most of the time; its stack holds an item's texts and a save row's
// script, which the save menu reads.
static const s32 kThreadPriority = 3000;
static const u64 kThreadStackBytes = 0x8000;

/** Waits for the rows' requests, the app's results, the save menu's release and Triangle. */
static void watch(void *unused)
{
    (void)unused;
    // The results taken but not shown yet; the thread's own, as nothing here writes to a global.
    char notices[NOTICES_BYTES];
    uint64_t taken = 0;
    // A request left from before the console started, as when the user used a row before the
    // restart that loads this plugin, would start the app unasked.
    cellFsUnlink(OWNS_LAUNCH_REQUEST);
    // Results left so would be shown as new.
    cellFsUnlink(NOTICES_FILE);
    // No save menu is open yet.
    cellFsUnlink(OWNS_MENU_TARGET);
    cellFsUnlink(OWNS_MENU_RELEASE);
    // The VSH's pad state and Triangle's last state: the thread's own. Without the pad state, the
    // save menu does not open; the rest works as before.
    const vsh_pad pad = pad_find();
    if (!pad_found(&pad))
    {
        owns_log("pad state not found: Triangle is off");
    }
    int triangle_was_down = 0;
    for (;;)
    {
        // The view's unload is asked for before the app a menu row asked for starts.
        menu_view_take_release();
        CellFsStat status;
        if (cellFsStat(OWNS_LAUNCH_REQUEST, &status) == 0)
        {
            cellFsUnlink(OWNS_LAUNCH_REQUEST);
            app_start();
        }
        if (cellFsStat(NOTICES_FILE, &status) == 0)
        {
            notices_take(notices, &taken);
        }
        // They show once the user is back on the XMB, with explore_plugin loaded, whose texts they
        // use; while the app runs they would show without them.
        if (taken > 0 && View_Find("explore_plugin") != 0)
        {
            notices_show(notices, taken);
            taken = 0;
        }
        for (int poll = 0; poll < kPadPollsPerRound; ++poll)
        {
            if (pad_triangle_pressed(&pad, &triangle_was_down))
            {
                save_menu_open_for_focused_item();
            }
            sysUsleep(kPadPollMicroseconds);
        }
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
