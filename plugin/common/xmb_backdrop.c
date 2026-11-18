// SPDX-License-Identifier: GPL-3.0-or-later
// explore_plugin's Info (4.82/4.88, 0x3f808 and 0x3f48c) turns the XMB into its full-screen
// backdrop through xmb_plugin's interface "XMB2", entry 7, (flags, mask): (0x200, 0x3c0) on open,
// which fades the bar and the columns away, and (0x40, 0x3c0) on close. ps3_savedata_plugin
// shows its pages, the delete question among them (0x1e7c8), with (0x400, 0xc00), and puts the
// XMB back with (0x40, 0xdc0). That mask leaves out Info's 0x200, so each look is put back with
// the mask of its own close, or Info's stays on. Info also moves explore_plugin
// (layer 2) to the top of xmb_plugin's "XMM0" layers (entry 8), which makes explore_plugin the
// active view; the save menu's pages never do, or the input would go to explore_plugin. The entry
// is checked word by word before it is called; a relocated immediate is checked by its opcode
// and registers.
#include "xmb_backdrop.h"

#include <stddef.h>
#include <stdint.h>

#include "owns_log.h"
#include "paf.h"

#define XMB2_INTERFACE 0x584d4232
#define DISPLAY_MODE_ENTRY 7
#define DISPLAY_INFO 0x200
#define DISPLAY_INFO_MASK 0x3c0
#define DISPLAY_UTILITY 0x400
#define DISPLAY_UTILITY_MASK 0xc00
#define DISPLAY_NORMAL 0x40
#define DISPLAY_UTILITY_CLEAR_MASK 0xdc0
#define WHOLE 0xffffffffu
#define IMMEDIATE 0xffff0000u

typedef struct
{
    uint32_t word;
    uint32_t mask;
} code_word;

// xmb_plugin 0x2838, the display mode: stdu r1,-0x290(r1); mflr r0; std r29,0x238(r1);
// lis r29,state@ha; stfd f29,0x278(r1); std r0,0x2a0(r1); std r18,0x1e0(r1); mr r18,r3.
static const code_word kDisplayMode[] = {
    {0xf821fd71u, WHOLE}, {0x7c0802a6u, WHOLE}, {0xfba10238u, WHOLE}, {0x3fa00000u, IMMEDIATE},
    {0xdba10278u, WHOLE}, {0xf80102a0u, WHOLE}, {0xfa4101e0u, WHOLE}, {0x7c721b78u, WHOLE},
};

#define WORDS(code) (sizeof code / sizeof code[0])

/** One of xmb_plugin's interface entries, once its code is the traced one; NULL otherwise. */
static const void *xmb_entry(int interface, int entry, const code_word *code, size_t count)
{
    const int xmb = View_Find("xmb_plugin");
    const void *const *table =
        xmb != 0 ? (const void *const *)plugin_GetInterface(xmb, interface) : NULL;
    const void *function = table != NULL ? table[entry] : NULL;
    if (function == NULL)
    {
        return NULL;
    }
    const uint32_t address = *(const uint32_t *)function;
    const volatile uint32_t *text = (const volatile uint32_t *)(uintptr_t)address;
    for (size_t i = 0; i < count; ++i)
    {
        if ((text[i] & code[i].mask) != code[i].word)
        {
            return NULL;
        }
    }
    return function;
}

/** Calls xmb_plugin's display mode with flags and a mask, once its code is the traced one. */
static int display_mode(int flags, int mask)
{
    typedef void (*display_mode_function)(int flags, int mask);
    const void *function =
        xmb_entry(XMB2_INTERFACE, DISPLAY_MODE_ENTRY, kDisplayMode, WORDS(kDisplayMode));
    if (function == NULL)
    {
        owns_log("XMB display mode skipped: not as traced");
        return 0;
    }
    ((display_mode_function)function)(flags, mask);
    owns_log_value("XMB display mode flags", (uint32_t)flags);
    owns_log_value("XMB display mode mask", (uint32_t)mask);
    return 1;
}

int xmb_backdrop_set(enum xmb_backdrop backdrop)
{
    return backdrop == XMB_BACKDROP_INFO ? display_mode(DISPLAY_INFO, DISPLAY_INFO_MASK)
                                         : display_mode(DISPLAY_UTILITY, DISPLAY_UTILITY_MASK);
}

int xmb_backdrop_clear(enum xmb_backdrop backdrop)
{
    return backdrop == XMB_BACKDROP_INFO ? display_mode(DISPLAY_NORMAL, DISPLAY_INFO_MASK)
                                         : display_mode(DISPLAY_NORMAL, DISPLAY_UTILITY_CLEAR_MASK);
}
