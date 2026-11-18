// SPDX-License-Identifier: GPL-3.0-or-later
// The save folders the app downloaded, registered in the system's saved data list: the Saved Data
// Utility lists the saves of the system's database, not the folders, so a save the app wrote as
// files shows there only once explore_plugin's "metagen_sync directory <folder> 1" has added it.

#ifndef OWNS_SAVE_REGISTER_H
#define OWNS_SAVE_REGISTER_H

#include <stdint.h>

// Room for one command: its words, the longest folder taken, and the NUL.
#define SAVE_REGISTER_COMMAND_BYTES 256
// explore_plugin's runner may read a command after taking it, so a command's text stays as it is
// until the runner has taken the next one, which it does only once it is done with this one.
#define SAVE_REGISTER_SLOTS 2

/** The registration's state: the watching thread's own, as nothing here writes to a global. */
typedef struct
{
    /** The request taken, its whole lines, on the VSH's heap; NULL when none is held. */
    char *batch;
    uint32_t length;
    /** Where the next line to register starts. */
    uint32_t next;
    /** How many rounds in a row explore_plugin did not take the next line's command. */
    int refusals;
    /** The slot the next command is written to; the other holds the one taken last. */
    int slot;
    char commands[SAVE_REGISTER_SLOTS][SAVE_REGISTER_COMMAND_BYTES];
} save_register;

/** Starts a registration's state with nothing held. */
void save_register_init(save_register *registry);

/** Whether folders wait to be registered: a request held, or one the app left. */
int save_register_waiting(const save_register *registry);

/**
 * Registers the next few folders of the app's request, taking the request from its file first
 * when none is held. A few a call, so that Triangle is not held up for long; a held request goes on
 * in the next call. Only for the XMB, with explore_plugin loaded, as it runs the commands, and no
 * game running, as the app may still be writing the request then.
 *
 * The folders are registered whichever user's home they are in: the plugin does not know who is
 * logged in. The app writes only its logged-in user's folders, and the request is taken as soon
 * as the user is back on the XMB, before another user can log in; only a batch cut short by a
 * shutdown is finished after the next start, whoever logs in then.
 */
void save_register_round(save_register *registry);

#endif
