// SPDX-License-Identifier: GPL-3.0-or-later
#include "menu_target.h"

#include <cell/fs/cell_fs_file_api.h>
#include <stdint.h>

#include "owns_log.h"
#include "owns_paths.h"
#include "owns_text.h"
#include "vsh_types.h"

// The head's letters: the place, the shown view's sort and group; then a tab and the record.
#define PLACE_AT 0
#define SORT_AT 1
#define GROUP_AT 2
#define RECORD_START 4
// A target far bigger than any record the app writes is not read.
#define TARGET_MAX_BYTES (64 * 1024)
// What peek reads: the head, a tab, the kind, a tab.
#define PEEK_BYTES 8

/** Whether a letter is one of enum menu_place's. */
static int is_place(char letter)
{
    return letter == MENU_PLACE_CLOUD || letter == MENU_PLACE_GAME_FOLDER ||
           letter == MENU_PLACE_OTHER;
}

/** Whether a target's text starts as the launcher writes it: head, tab, kind, tab. */
static int head_valid(const char *text, int64_t length)
{
    return length > RECORD_START + 1 && is_place(text[PLACE_AT]) &&
           (xmb_view_is_sort(text[SORT_AT]) || text[SORT_AT] == XMB_VIEW_UNKNOWN) &&
           (xmb_view_is_group(text[GROUP_AT]) || text[GROUP_AT] == XMB_VIEW_UNKNOWN) &&
           text[RECORD_START - 1] == '\t' &&
           (text[RECORD_START] == ROW_RECORD_GAME || text[RECORD_START] == ROW_RECORD_SAVE) &&
           text[RECORD_START + 1] == '\t';
}

/** The head of a valid target's text. */
static menu_head head_of(const char *text)
{
    const menu_head head = {
        (enum menu_place)text[PLACE_AT], {text[SORT_AT], text[GROUP_AT]}, text[RECORD_START]};
    return head;
}

int menu_head_view_known(const menu_head *head)
{
    return head->shown.sort != XMB_VIEW_UNKNOWN && head->shown.group != XMB_VIEW_UNKNOWN;
}

int menu_target_write(enum menu_place place, xmb_view shown, const char *record)
{
    const size_t length = owns_text_length(record);
    char *text = (char *)vsh_new((unsigned int)(RECORD_START + length + 1));
    if (text == NULL)
    {
        return 0;
    }
    text[PLACE_AT] = (char)place;
    text[SORT_AT] = shown.sort;
    text[GROUP_AT] = shown.group;
    text[RECORD_START - 1] = '\t';
    for (size_t i = 0; i <= length; ++i)
    {
        text[RECORD_START + i] = record[i];
    }
    const int written = owns_write_file(OWNS_MENU_TARGET, text);
    vsh_delete(text);
    if (!written)
    {
        owns_log("target not written");
        cellFsUnlink(OWNS_MENU_TARGET);
    }
    return written;
}

int menu_target_peek(menu_head *head)
{
    char text[PEEK_BYTES];
    const int64_t read = owns_read_file(OWNS_MENU_TARGET, text, sizeof text);
    if (!head_valid(text, read))
    {
        return 0;
    }
    *head = head_of(text);
    return 1;
}

int menu_target_read(menu_target *target)
{
    target->text = NULL;
    CellFsStat status;
    if (cellFsStat(OWNS_MENU_TARGET, &status) != 0 || status.st_size > TARGET_MAX_BYTES)
    {
        return 0;
    }
    char *text = (char *)vsh_new((unsigned int)status.st_size + 1);
    if (text == NULL)
    {
        return 0;
    }
    int64_t length = owns_read_file(OWNS_MENU_TARGET, text, status.st_size + 1);
    // The record is one line; a line end the file may keep is not part of its last field.
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r'))
    {
        text[--length] = '\0';
    }
    if (!head_valid(text, length))
    {
        vsh_delete(text);
        return 0;
    }
    target->text = text;
    target->head = head_of(text);
    target->field_count = row_record_split(text + RECORD_START, target->fields, ROW_FIELDS_MAX);
    if (!row_record_valid(target->head.kind, target->field_count))
    {
        menu_target_free(target);
        return 0;
    }
    return 1;
}

void menu_target_free(menu_target *target)
{
    if (target->text != NULL)
    {
        vsh_delete(target->text);
        target->text = NULL;
    }
}

const char *menu_target_field(const menu_target *target, int field)
{
    return field >= 0 && field < target->field_count ? target->fields[field] : "";
}
