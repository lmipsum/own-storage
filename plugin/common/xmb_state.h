// SPDX-License-Identifier: GPL-3.0-or-later
// What the XMB had the input on before the save menu took it, and giving it back so. The launcher
// saves it as the menu opens; the save menu's view, or recovery, gives it back as the menu or its
// last page closes. It is kept in a file, as the modules write no globals and are two.

#ifndef OWNS_XMB_STATE_H
#define OWNS_XMB_STATE_H

/** Saves explore_plugin's view, its active page node and its focused list. */
void xmb_state_save(void);

/**
 * Gives the input back to the XMB's list: makes explore_plugin the active view, activates the page
 * node saved (else its list page by name), and focuses the list saved while that is still one of
 * explore_plugin's lists (else the level the user is on). xmb_plugin's page is the fallback. The
 * saved state is used up.
 */
void xmb_state_restore(void);

#endif
