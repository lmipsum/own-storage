// SPDX-License-Identifier: GPL-3.0-or-later
// How the XMB lists the saves in the cloud, as the user chose it in the save menu: two letters in
// the app's folder, the sort's and the group's, as the app's ParseXmbView reads them.

#ifndef OWNS_XMB_VIEW_H
#define OWNS_XMB_VIEW_H

// The letters in the order the Sort By and Group Content submenus list them: newest first, oldest
// first, by title, by subtitle; a folder per game, all in one list.
#define XMB_VIEW_SORT_LETTERS "dots"
#define XMB_VIEW_GROUP_LETTERS "ta"
#define XMB_VIEW_SORTS 4
#define XMB_VIEW_GROUPS 2
// The group of a folder per game, which the list files of each game's saves are always in.
#define XMB_VIEW_BY_TITLE 't'
// The letter of a sort or a group that is not known.
#define XMB_VIEW_UNKNOWN '-'

/** A view: one of XMB_VIEW_SORT_LETTERS and one of XMB_VIEW_GROUP_LETTERS. */
typedef struct
{
    char sort;
    char group;
} xmb_view;

/** Whether a letter is one of a sort's. */
int xmb_view_is_sort(char letter);

/** Whether a letter is one of a group's. */
int xmb_view_is_group(char letter);

/** The view the user chose last; newest first by title, as the app has it, when none is kept. */
xmb_view xmb_view_read(void);

/**
 * Keeps a view for the app.
 * @return 1 when it was written
 */
int xmb_view_write(xmb_view view);

#endif
