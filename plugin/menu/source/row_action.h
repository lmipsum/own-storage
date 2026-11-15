// SPDX-License-Identifier: GPL-3.0-or-later
// What a pushed row of the save menu asks the app for.

#ifndef OWNS_ROW_ACTION_H
#define OWNS_ROW_ACTION_H

#include <stdint.h>

/**
 * Leaves the row's action for the menu's save in the app's action file, where the app's
 * TakeXmbAction reads it as it starts: "info:<NAME>" or "delete:<NAME>".
 * @param row one of enum owns_menu_row
 * @return 1 when it was written, and the app is to start
 */
int row_action_write(uint32_t row);

#endif
