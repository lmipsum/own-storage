// SPDX-License-Identifier: GPL-3.0-or-later
// The XMB as a full-screen page's backdrop, as explore_plugin's Information shows it.

#ifndef OWNS_XMB_BACKDROP_H
#define OWNS_XMB_BACKDROP_H

/** The XMB's looks behind a full-screen page, as xmb_plugin's display mode sets them. */
enum xmb_backdrop
{
    // explore_plugin's Information: the bar and the columns fade away, the wallpaper stays.
    XMB_BACKDROP_INFO,
    // The Saved Data Utility's pages, its delete question among them.
    XMB_BACKDROP_UTILITY,
};

/**
 * Sets xmb_plugin's display mode. Nothing happens unless xmb_plugin's code is the traced one;
 * the log says so.
 * @return 1 when xmb_plugin was asked
 */
int xmb_backdrop_set(enum xmb_backdrop backdrop);

/**
 * Puts the XMB back as it was before a backdrop, as the page that set it does on its close.
 * @return 1 when xmb_plugin was asked
 */
int xmb_backdrop_clear(enum xmb_backdrop backdrop);

#endif
