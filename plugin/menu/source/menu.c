// SPDX-License-Identifier: GPL-3.0-or-later
// The module of the save menu's plugin view, which paf loads with owns_menu.rco when the launcher
// asks for it. It registers itself the way the system's plugins (xmb_plugin, xai_plugin) do:
// module_start hands paf the module's interface 1, {init, start, stop, exit}; init gets the new
// view and binds the RCO's "native:/Owns::*" events to the handlers here.
//
// The module's only segment is read-only once loaded, so nothing here writes to a global.

#include <cell/fs/cell_fs_file_api.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/prx_module.h>

#include "menu_release.h"
#include "owns_log.h"
#include "owns_paths.h"
#include "owns_text.h"
#include "paf.h"
#include "row_action.h"
#include "row_labels.h"
#include "vshcommon.h"
#include "xmb_return.h"

// The row a cbScrollIn brings in (the event's +0xc) and the row cbPush pushed (the list's
// +0x510), as explore_plugin's Menu reads them.
#define EVENT_ROW 0xc
#define LIST_SELECTED_ROW 0x510

/** paf::EventFunctionEntry: a handler for an RCO event; a NULL name ends a table. */
typedef struct
{
    const char *name;
    void (*handler)(void *widget, void *event, void *data);
    uint32_t zero;
} vsh_event_entry;

/** Closes the menu, gives the input back to the XMB, and asks the launcher to unload the view. */
static void finish_menu(void)
{
    const int view = View_Find(OWNS_MENU_VIEW);
    if (view != 0)
    {
        owns_log_value("option menu closed",
                       (uint32_t)OptionMenu_Close(view, OWNS_MENU_PAGE, NULL));
    }
    xmb_return_focus();
    cellFsUnlink(OWNS_MENU_TARGET);
    menu_release_request();
}

/** X on a row: leaves its action for the app, closes the menu, then starts the app. */
static void on_push(void *widget, void *event, void *data)
{
    (void)event;
    (void)data;
    const int start_app =
        widget != NULL &&
        row_action_write(*(const uint32_t *)((uintptr_t)widget + LIST_SELECTED_ROW));
    finish_menu();
    // As a row's script asks for the app.
    if (start_app && !owns_write_file(OWNS_LAUNCH_REQUEST, "launch"))
    {
        owns_log("launch not requested");
    }
}

/** O: closes the menu. */
static void on_cancel(void *widget, void *event, void *data)
{
    (void)widget;
    (void)event;
    (void)data;
    finish_menu();
}

/** A row scrolls into a list: it gets its label. */
static void on_scroll_in(void *widget, void *event, void *data)
{
    (void)data;
    if (widget != NULL && event != NULL)
    {
        row_labels_set(widget, *(const uint32_t *)((uintptr_t)event + EVENT_ROW));
    }
}

/** The page's other events, which the option menu handles itself. */
static void on_other_event(void *widget, void *event, void *data)
{
    (void)widget;
    (void)event;
    (void)data;
}

// The page's events by name, without "native:/", as View_SetEventHandler takes them.
static const vsh_event_entry kEvents[] = {
    {"Owns::cbPush", on_push, 0},
    {"Owns::cbCancel", on_cancel, 0},
    {"Owns::cbContextMenu", on_other_event, 0},
    {"Owns::cbFocusLeft", on_other_event, 0},
    {"Owns::cbFocusRight", on_other_event, 0},
    {"Owns::cbFocusUp", on_other_event, 0},
    {"Owns::cbFocusDown", on_other_event, 0},
    {"Owns::cbScrollIn", on_scroll_in, 0},
    {"Owns::cbScrollOut", on_other_event, 0},
    {NULL, NULL, 0},
};

/** Interface 1, entry 0: paf calls it with the new view once the module is started. */
static void view_init(int view)
{
    View_SetEventHandler(view, kEvents);
}

/** Interface 1, entry 1. */
static int view_start(void)
{
    return 0;
}

/** Interface 1, entry 2. */
static int view_stop(void)
{
    return 0;
}

/** Interface 1, entry 3. */
static void view_exit(void) {}

// What paf::Module::SetInterface(module, 1, ...) gets from the system's plugins.
static const void *const kInterface[] = {
    (const void *)view_init,
    (const void *)view_start,
    (const void *)view_stop,
    (const void *)view_exit,
};

int module_start(size_t args, void *argp)
{
    // paf's Module::Start passes 4 bytes: the paf::Module, which xmb_plugin and xai_plugin hand
    // straight to Module::SetInterface.
    const int module = (args >= 4 && argp != NULL) ? *(const int *)argp : 0;
    owns_log_value("menu module", (uint32_t)module);
    // Without it the view has no interface; the module stays, as paf still holds it.
    if (module != 0)
    {
        Module_SetInterface(module, 1, kInterface);
    }
    return SYS_PRX_RESIDENT;
}

int module_stop(size_t args, void *argp)
{
    (void)args;
    (void)argp;
    return 0;
}
