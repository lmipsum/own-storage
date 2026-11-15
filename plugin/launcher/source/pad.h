// SPDX-License-Identifier: GPL-3.0-or-later
// The Triangle button, read from the VSH's own copy of the pad state, so nothing is taken from
// the XMB's input. Where the VSH keeps that copy differs between firmware versions; it is found at
// run time by the code of the VSH's pad poll, as sLaunch finds it.

#ifndef OWNS_PAD_H
#define OWNS_PAD_H

#include <stdint.h>

/** Where the VSH keeps its pad state; both zero when its code was not recognised. */
typedef struct
{
    /** The last CellPadData the VSH read. */
    uint32_t data;
    /** The word of the buttons held, DIGITAL1 | DIGITAL2 << 8. */
    uint32_t held;
} vsh_pad;

/** Finds the VSH's pad state; reads the VSH's code, so it is slow and done once. */
vsh_pad pad_find(void);

/** Whether the VSH's pad state was found; without it Triangle never reads as pressed. */
int pad_found(const vsh_pad *pad);

/**
 * Whether Triangle went down since the last call.
 * @param was_down Triangle's state at the last call; updated
 */
int pad_triangle_pressed(const vsh_pad *pad, int *was_down);

#endif
