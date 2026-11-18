// SPDX-License-Identifier: GPL-3.0-or-later
// How the save menu ends, whichever of its menu or pages is the last open: the XMB gets its input
// back, the launcher unloads the view, and what the user chose follows.

#ifndef OWNS_MENU_FINISH_H
#define OWNS_MENU_FINISH_H

/** What follows once the menu is gone. */
enum menu_end
{
    MENU_END_CLOSE,
    /** The app starts for the action left for it. */
    MENU_END_START_APP,
    /** The launcher switches the XMB's list to the view the user chose. */
    MENU_END_SWITCH_LIST,
    /** A page is open, which ends the menu itself when it closes. */
    MENU_END_PAGE_OPEN,
};

/** Ends the menu whose option menu is closed already and which has no page open. */
void menu_end_closed(enum menu_end end);

/** Closes the option menu, from whichever of its levels, and ends the menu. */
void menu_close_menu(int view, enum menu_end end);

/** The menu's full-screen pages that can be open. */
enum menu_pages
{
    /** The information page. */
    MENU_PAGES_INFO,
    /** A delete question's page and its dialog's page. */
    MENU_PAGES_QUESTION,
};

/** Closes the menu's full-screen pages, puts the XMB's looks back and ends the menu. */
void menu_close_pages(int view, enum menu_pages pages, enum menu_end end);

#endif
