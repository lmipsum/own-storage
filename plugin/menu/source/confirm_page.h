// SPDX-License-Identifier: GPL-3.0-or-later
// A question before a delete, as the Saved Data Utility asks it: what is to be deleted on a
// full-screen page that is a copy of its own (ps3_savedata_plugin's page_plugin_delete), and over
// it the system's Yes/No dialog.

#ifndef OWNS_CONFIRM_PAGE_H
#define OWNS_CONFIRM_PAGE_H

/** The delete page's texts, by their widgets in owns_menu.json. */
enum confirm_text
{
    CONFIRM_TITLE,
    CONFIRM_SUBTITLE,
    CONFIRM_DATE,
    CONFIRM_SIZE,
    CONFIRM_DETAIL,
    CONFIRM_TEXTS
};

/** A text of the page: UTF-8, or UTF-16 when units is set; an empty or NULL one is left empty. */
typedef struct
{
    const char *text;
    const unsigned short *units;
} confirm_value;

/**
 * The dialog's answer, in paf's job once the dialog has gone by itself.
 * @param result 0 for Yes
 * @param data the view, as an integer
 */
typedef void (*confirm_answer)(int result, void *data);

/**
 * Opens the page with its texts and the dialog with the question over it.
 * @param values the page's texts, CONFIRM_TEXTS of them
 * @param question UTF-16, NUL-ended; the dialog copies it
 * @param on_answer gets the answer, which it hands to confirm_page_finish
 * @return 0 when not even the page opened, and nothing is shown; 1 otherwise, the pages already
 *         closed again when the dialog could not open
 */
int confirm_page_open(int view, const confirm_value *values, const unsigned short *question,
                      confirm_answer on_answer);

/**
 * Takes a dialog's answer and closes the pages. Yes leaves an action for the app, its argument a
 * field of the menu's target, and starts the app.
 * @param result the dialog's, 0 for Yes
 * @param action one of app_action.h's
 * @param field the target's field (enum row_field) the action names
 */
void confirm_page_finish(int view, int result, const char *action, int field);

#endif
