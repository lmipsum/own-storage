// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/XmbLists.h"
#include "sync/LocalStorage.h"

#include <string>

/**
 * Puts the Own Storage folder in the XMB's Game column, as webMAN MOD's installer puts its own:
 * the system's category_game.xml gets one line, written through the flash mounted writable, with
 * the old file kept as category_game.xml.owns.bak. The new file is written and checked before the
 * old one is moved aside, so the Game column is never left without its file. A file that has the
 * line already, as after every run but the first and until a system update, is left alone.
 * @param storage the console's disks, the flash among them
 * @param place where the lists are
 * @return false when the line is not in the file and could not be written
 */
bool InstallXmbFolder(LocalStorage &storage, const XmbPlace &place);

/**
 * Writes the XMB's lists, and the rows' webMAN scripts in place of those of the last lists.
 * @param storage the console's hard disk
 * @param place where the lists go
 * @param lists the files' content
 * @return false when one could not be written
 */
bool WriteXmbLists(LocalStorage &storage, const XmbPlace &place, const XmbLists &lists);

/**
 * Takes the action a row of the XMB left for the app, so it is done once. An action left long ago,
 * when webMAN wrote it but did not start the app, is dropped rather than done on a later start.
 * @param storage the console's hard disk
 * @param place where the action file is
 * @return the action; empty when the app was started otherwise
 */
std::string TakeXmbAction(LocalStorage &storage, const XmbPlace &place);
