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

// More open list levels than the XMB has.
#define EXPLORE_LISTS_MAX 16

/**
 * The open lists of explore_plugin's current category, one a folder level, the deepest last.
 * Every step is checked to be as traced; nothing is written.
 * @return their count; -1 when explore_plugin is not as traced, or they are more than room
 */
int explore_query_lists(uint32_t *lists, int room);

/**
 * The stored query URL of one of explore_query_lists' lists.
 * @return 1 when it has one, as traced
 */
int explore_query_list_url(uint32_t list, explore_url *url);

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

// How long a wait on explore_plugin lasts: a check every 0.1 s, at most 2 s.
#define EXPLORE_WAIT_MICROSECONDS 100000
#define EXPLORE_WAIT_ROUNDS 20

/**
 * Hands explore_plugin a command, waiting while its runner is busy with another: the runner takes
 * one command at a time, and refuses the next until it is done with the one before.
 * @param command a text that stays, as for explore_command
 * @return 1 when explore_plugin took it; 0 when its runner stayed busy
 */
int explore_command_wait(const char *command);

#endif
