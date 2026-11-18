// SPDX-License-Identifier: GPL-3.0-or-later
// explore_plugin's XMB lists: one PhXmList per opened folder level, on its list page.

#ifndef OWNS_XMB_LIST_H
#define OWNS_XMB_LIST_H

// explore_plugin's page of the XMB's lists.
#define XMB_LIST_PAGE "expage_explore_list"

// The most lists xmb_lists finds: more levels than the XMB opens.
#define XMB_LISTS_MAX 8

/**
 * explore_plugin's PhXmLists on its list page, depth first.
 * @return how many were stored in lists; 0 when explore_plugin shows none
 */
int xmb_lists(void **lists, int room);

/** The PhXmList of the level the user is on; NULL when explore_plugin shows none. */
void *xmb_focused_list(void);

#endif
