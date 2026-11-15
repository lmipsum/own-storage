// SPDX-License-Identifier: GPL-3.0-or-later
// paf::View::InitParam and its use by xmb_plugin, which loads each of its plugins with
// View_LoadAsync (4.82/4.88 vsh.self).
#include "menu_view.h"

#include <cell/fs/cell_fs_file_api.h>
#include <stddef.h>
#include <stdint.h>

#include "menu_release.h"
#include "option_menu.h"
#include "owns_log.h"
#include "owns_paths.h"
#include "owns_text.h"
#include "paf.h"
#include "vsh_types.h"
#include "xmb_return.h"

#define MENU_PARENT "xmb_plugin"
#define PLUGIN_DIR OWNS_APP_DIR "plugins/"
#define MENU_RCO PLUGIN_DIR "owns_menu.rco"
#define MENU_SPRX PLUGIN_DIR "owns_menu.sprx"
// Below explore_plugin's 0x31, so the menu is drawn over the XMB, and above the clock's 0x14; at
// the default 100 the XMB's background covers it.
#define MENU_DRAW_PRIORITY 0x30
// What xmb_plugin sets for every plugin of its table.
#define XMB_PLUGIN_FLAG 1

/** paf::View::InitParam, 0xcc bytes; its constructor sets the defaults noted. */
typedef struct
{
    vsh_string name;          // 0x00: the view's name, for View::Find
    vsh_string parent;        // 0x1c: "xmb_plugin" for xmb's plugins
    uint32_t callbacks[4];    // 0x38: 0
    vsh_string rco;           // 0x48: resource file
    uint32_t unknown64;       // 0x64: 0
    uint32_t draw_priority;   // 0x68: 100; explore_plugin 0x31
    uint32_t unknown6c;       // 0x6c: -1
    int32_t memory_container; // 0x70: -1
    uint32_t heap_size;       // 0x74: 0, paf's heap
    uint32_t heap;            // 0x78: 0
    uint32_t graphics_size;   // 0x7c: 0, shared
    uint32_t unknown80;       // 0x80: 0
    uint32_t unknown84;       // 0x84: 0
    uint32_t unknown88;       // 0x88: 0xff00
    uint32_t arguments[3];    // 0x8c: std::map, its head node on the heap
    vsh_string sprx;          // 0x98: module file
    uint32_t xmb_flag;        // 0xb4: 0
    uint32_t module_flags;    // 0xb8: 0; passed to the module's load
    uint32_t flags;           // 0xbc: 0xc
    uint32_t unknownc0[3];    // 0xc0: 0
} vsh_view_init_param;

_Static_assert(sizeof(vsh_view_init_param) == 0xcc, "paf::View::InitParam is 0xcc bytes");

/** Sets an empty, just constructed VSH string, as basic_string::assign(const char *) would. */
static int vsh_string_set(vsh_string *string, const char *text)
{
    const size_t length = owns_text_length(text);
    char *target = string->data.text;
    if (length > VSH_STRING_IN_PLACE)
    {
        target = (char *)vsh_new((unsigned int)length + 1);
        if (target == NULL)
        {
            return 0;
        }
        string->data.heap = target;
        string->capacity = (uint32_t)length;
    }
    for (size_t i = 0; i <= length; ++i)
    {
        target[i] = text[i];
    }
    string->size = (uint32_t)length;
    return 1;
}

/** Frees what a VSH string holds on the heap, as its destructor would. */
static void vsh_string_free(vsh_string *string)
{
    if (string->capacity > VSH_STRING_IN_PLACE)
    {
        vsh_delete(string->data.heap);
    }
    string->capacity = VSH_STRING_IN_PLACE;
    string->size = 0;
}

/** paf's handler once the view is up, in paf's job: opens the menu, else gives it up again. */
static void on_view_loaded(void *view)
{
    owns_log_value("menu view loaded", (uint32_t)(uintptr_t)view);
    if (view == NULL || !option_menu_open((int)(uintptr_t)view))
    {
        xmb_return_focus();
        menu_release_request();
    }
}

/** paf's handler once the view is gone: gets its name. */
static void on_view_unloaded(const char *name)
{
    owns_log_value("menu view unloaded", (uint32_t)(uintptr_t)name);
}

/** Whether a file is there. */
static int file_exists(const char *path)
{
    CellFsStat status;
    return cellFsStat(path, &status) == 0;
}

int menu_view_loaded(void)
{
    return View_Find(OWNS_MENU_VIEW) != 0;
}

void menu_view_load(void)
{
    if (!file_exists(MENU_RCO) || !file_exists(MENU_SPRX))
    {
        owns_log("menu view files missing");
        return;
    }
    vsh_view_init_param init_param;
    View_InitParam_ctor(&init_param);
    if (vsh_string_set(&init_param.name, OWNS_MENU_VIEW) &&
        vsh_string_set(&init_param.parent, MENU_PARENT) &&
        vsh_string_set(&init_param.rco, MENU_RCO) && vsh_string_set(&init_param.sprx, MENU_SPRX))
    {
        init_param.xmb_flag = XMB_PLUGIN_FLAG;
        init_param.module_flags = XMB_PLUGIN_FLAG;
        init_param.draw_priority = MENU_DRAW_PRIORITY;
        View_LoadAsync(&init_param, (void *)on_view_loaded, 1);
    }
    // InitParam has no exported destructor; paf copied what it needs. The map's head node, which
    // the constructor allocates, stays: how to free it is not known, and it is 0x50 bytes a load.
    vsh_string_free(&init_param.name);
    vsh_string_free(&init_param.parent);
    vsh_string_free(&init_param.rco);
    vsh_string_free(&init_param.sprx);
}

void menu_view_take_release(void)
{
    if (!file_exists(OWNS_MENU_RELEASE))
    {
        return;
    }
    cellFsUnlink(OWNS_MENU_RELEASE);
    if (menu_view_loaded())
    {
        View_UnloadAsync(OWNS_MENU_VIEW, (void *)on_view_unloaded, 1);
    }
}
