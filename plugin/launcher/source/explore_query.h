// SPDX-License-Identifier: GPL-3.0-or-later
// The query explore_plugin keeps for the XMB list the user is on: the xmb:// URL it rebuilds the
// list from on reload_list, which names the list file of the item that opened the list.

#ifndef OWNS_EXPLORE_QUERY_H
#define OWNS_EXPLORE_QUERY_H

#include <stdint.h>

// The longest URL taken; explore_plugin's of the app's lists are far shorter.
#define EXPLORE_URL_MAX 256

/** A URL in explore_plugin's memory: its first byte's address and its bytes, without a NUL. */
typedef struct
{
    uint32_t address;
    uint32_t length;
} explore_url;

/**
 * Finds the stored query URL of the deepest open list of explore_plugin's current category, the
 * one the user is on. Every step is checked to be as traced; nothing is written.
 * @return 1 when found
 */
int explore_query_url(explore_url *url);

/**
 * Copies a URL out of explore_plugin's memory.
 * @param text receives it, NUL-ended; EXPLORE_URL_MAX bytes
 */
void explore_url_copy(const explore_url *url, char *text);

/**
 * Hands explore_plugin an XMB command for its command runner, as explore_plugin's own scripts do.
 * explore_plugin may read it after the call, so it must be a text that stays.
 * @return 1 when explore_plugin took it
 */
int explore_command(const char *command);

#endif
