// SPDX-License-Identifier: GPL-3.0-or-later
// The module of the save menu's plugin view, which paf loads with owns_menu.rco when the launcher
// asks for it. It registers itself the way the system's plugins (xmb_plugin, xai_plugin) do:
// module_start hands paf the module's interface 1, {init, start, stop, exit}; init gets the new
// view and binds the RCO's "native:/Owns::*" events to the handlers here.
//
// The module's only segment is read-only once loaded, so nothing here writes to a global; what
// the menu acts on is read from the launcher's target as each event needs it.

#include <stddef.h>
#include <stdint.h>
#include <sys/prx_module.h>

#include "game_menu.h"
#include "menu_finish.h"
#include "menu_rows.h"
#include "menu_target.h"
#include "option_menu_close.h"
#include "owns_log.h"
#include "owns_paths.h"
#include "paf.h"
#include "row_labels.h"
#include "save_menu.h"
#include "submenu.h"
#include "view_choice.h"

// The row a cbScrollIn brings in, as explore_plugin's Menu reads it (the event's +0xc).
#define EVENT_ROW 0xc
// explore_plugin's Menu and Info handlers take no event whose flags (+8) have bit 0 set.
#define EVENT_FLAGS 8
#define EVENT_FLAG_IGNORED 1

/** paf::EventFunctionEntry: a handler for an RCO event; a NULL name ends a table. */
typedef struct
{
    const char *name;
    void (*handler)(void *widget, void *event, void *data);
    uint32_t zero;
} vsh_event_entry;

/** Whether an event is one explore_plugin's Menu and Info handlers ignore. */
static int event_ignored(const void *event)
{
    return event != NULL &&
           (*(const uint8_t *)((uintptr_t)event + EVENT_FLAGS) & EVENT_FLAG_IGNORED) != 0;
}

/** Where the menu's row is, the view its list shows and the row's kind, which the rows depend on.
 */
typedef menu_head menu_context;

/** The menu's context from the launcher's target; 0 when the target is gone. */
static int read_context(menu_context *context)
{
    return menu_target_peek(context);
}

/** The section of the open submenu, by the first level's upper row it belongs to. */
static enum menu_section open_section(const menu_context *context)
{
    return menu_section_at(context->place, submenu_parent_row());
}

/** A lower-list row on the first level: the menu goes, and the row's save or game acts. */
static void push_action(int view, const menu_context *context, uint32_t row)
{
    const enum menu_action action = menu_action_at(context->kind, row);
    menu_target target;
    if (action == MENU_ACTIONS || !menu_target_read(&target))
    {
        menu_close_menu(view, MENU_END_CLOSE);
        return;
    }
    // The view stays for a page, which ends the menu itself when it closes.
    option_menu_close(view);
    const enum menu_end end = target.head.kind == ROW_RECORD_SAVE
                                  ? save_menu_run(view, action, &target)
                                  : game_menu_run(view, action, &target);
    menu_target_free(&target);
    menu_end_closed(end);
}

/**
 * A submenu's row: its choice is kept for the app, and the list switches to it as the menu goes.
 * The switch is asked for even when the choice is the one kept, as the list may show another
 * view; the launcher leaves a list that shows the choice already as it is.
 */
static void push_choice(int view, const menu_context *context, uint32_t row)
{
    const int kept = view_choice_take(open_section(context), context, row);
    menu_close_menu(view, kept ? MENU_END_SWITCH_LIST : MENU_END_CLOSE);
}

/**
 * X on a row. First level, lower list: the row's save or game acts. First level, upper list: the
 * section opens its submenu. A submenu's row: its choice is taken, and the menu closes.
 */
static void on_push(void *widget, void *event, void *data)
{
    (void)data;
    uint32_t level = 0;
    int upper = 0;
    menu_context context;
    const int view = View_Find(OWNS_MENU_VIEW);
    if (widget == NULL || view == 0 || event_ignored(event) ||
        !submenu_list_place(widget, &level, &upper))
    {
        return;
    }
    const uint32_t row = submenu_selected_row(widget);
    if (!read_context(&context))
    {
        menu_close_menu(view, MENU_END_CLOSE);
    }
    else if (level > 0)
    {
        push_choice(view, &context, row);
    }
    else if (upper)
    {
        submenu_open(view, menu_section_at(context.place, row), &context);
    }
    else
    {
        push_action(view, &context, row);
    }
}

/** O (explore_plugin's Menu::cbCancel, 0x5ead4): closes a submenu, else the menu. */
static void on_cancel(void *widget, void *event, void *data)
{
    (void)widget;
    (void)data;
    const int view = View_Find(OWNS_MENU_VIEW);
    if (view == 0 || event_ignored(event))
    {
        return;
    }
    const uint32_t level = submenu_level();
    if (level > 0)
    {
        submenu_close(view, level);
        return;
    }
    menu_close_menu(view, MENU_END_CLOSE);
}

/** Left (Menu::cbFocusLeft, 0x5eb60): closes a submenu, as O does; nothing on the first level. */
static void on_focus_left(void *widget, void *event, void *data)
{
    (void)widget;
    (void)data;
    const int view = View_Find(OWNS_MENU_VIEW);
    const uint32_t level = submenu_level();
    if (view != 0 && !event_ignored(event) && level > 0)
    {
        submenu_close(view, level);
    }
}

/** Right (Menu::cbFocusRight, 0x5eb3c): on a first-level upper row, opens its submenu, as X. */
static void on_focus_right(void *widget, void *event, void *data)
{
    (void)data;
    uint32_t level = 0;
    int upper = 0;
    menu_context context;
    const int view = View_Find(OWNS_MENU_VIEW);
    if (widget != NULL && view != 0 && !event_ignored(event) &&
        submenu_list_place(widget, &level, &upper) && level == 0 && upper && read_context(&context))
    {
        submenu_open(view, menu_section_at(context.place, submenu_selected_row(widget)), &context);
    }
}

/**
 * Up past the lower list's first row (Menu::cbFocusUp, 0x5e190): into the upper list, on its
 * last row, the one next to the lower list.
 */
static void on_focus_up(void *widget, void *event, void *data)
{
    (void)widget;
    (void)data;
    menu_context context;
    if (event_ignored(event) || !read_context(&context))
    {
        return;
    }
    const uint32_t level = submenu_level();
    const int count =
        level > 0 ? menu_choice_count(open_section(&context)) : menu_section_count(context.place);
    if (count > 0)
    {
        submenu_focus(level, SUBMENU_UPPER_LIST, count - 1);
    }
}

/** Down past the upper list's last row (Menu::cbFocusDown, 0x5e240): into the lower list. */
static void on_focus_down(void *widget, void *event, void *data)
{
    (void)widget;
    (void)data;
    menu_context context;
    // A submenu's lower list has no rows.
    if (!event_ignored(event) && submenu_level() == 0 && read_context(&context) &&
        menu_action_count(context.kind) > 0)
    {
        submenu_focus(0, SUBMENU_LOWER_LIST, 0);
    }
}

/** A row scrolls into a list: it gets its label, of its level, list and submenu. */
static void on_scroll_in(void *widget, void *event, void *data)
{
    (void)data;
    uint32_t level = 0;
    int upper = 0;
    menu_context context;
    if (widget != NULL && event != NULL && submenu_list_place(widget, &level, &upper) &&
        read_context(&context))
    {
        const enum menu_section section = level > 0 ? open_section(&context) : MENU_SECTIONS;
        row_labels_set(widget, level, upper, section, context.place, context.kind,
                       *(const uint32_t *)((uintptr_t)event + EVENT_ROW));
    }
}

/** O on the information page: closes it and ends the menu. */
static void on_info_cancel(void *widget, void *event, void *data)
{
    (void)widget;
    (void)data;
    const int view = View_Find(OWNS_MENU_VIEW);
    if (view != 0 && !event_ignored(event))
    {
        menu_close_pages(view, 0, MENU_END_CLOSE);
    }
}

/** The pages' other events, which paf and the option menu handle themselves. */
static void on_other_event(void *widget, void *event, void *data)
{
    (void)widget;
    (void)event;
    (void)data;
}

// The pages' events by name, without "native:/", as View_SetEventHandler takes them.
static const vsh_event_entry kEvents[] = {
    {"Owns::cbPush", on_push, 0},
    {"Owns::cbCancel", on_cancel, 0},
    {"Owns::cbContextMenu", on_other_event, 0},
    {"Owns::cbFocusLeft", on_focus_left, 0},
    {"Owns::cbFocusRight", on_focus_right, 0},
    {"Owns::cbFocusUp", on_focus_up, 0},
    {"Owns::cbFocusDown", on_focus_down, 0},
    {"Owns::cbScrollIn", on_scroll_in, 0},
    {"Owns::cbScrollOut", on_other_event, 0},
    {"Owns::cbInfoInit", on_other_event, 0},
    {"Owns::cbInfoActivate", on_other_event, 0},
    {"Owns::cbInfoCancel", on_info_cancel, 0},
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
