// SPDX-License-Identifier: GPL-3.0-or-later
// Handing the input back to the XMB once the save menu is done.

#ifndef OWNS_XMB_RETURN_H
#define OWNS_XMB_RETURN_H

/**
 * Gives the input back to the XMB's list. The save menu's view was made the active one before
 * the option menu opened, and the option menu's close leaves the input on its page; explore_plugin
 * focuses its list page the same way after its own menu. xmb_plugin's page is the fallback.
 */
void xmb_return_focus(void);

#endif
