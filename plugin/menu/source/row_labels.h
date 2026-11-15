// SPDX-License-Identifier: GPL-3.0-or-later
// The save menu's row labels: the system's own words where it has them, else the RCO's.

#ifndef OWNS_ROW_LABELS_H
#define OWNS_ROW_LABELS_H

#include <stdint.h>

/**
 * Sets a row's label on the menu list that brings the row in.
 * @param list the list, as its cbScrollIn event gets it
 * @param row one of enum owns_menu_row
 */
void row_labels_set(void *list, uint32_t row);

#endif
