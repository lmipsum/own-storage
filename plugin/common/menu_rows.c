// SPDX-License-Identifier: GPL-3.0-or-later
#include "menu_rows.h"

#include "row_record.h"
#include "xmb_view.h"

#define COUNT(table) ((int)(sizeof table / sizeof table[0]))

static const enum menu_action kSaveActions[] = {MENU_ACTION_INFO, MENU_ACTION_DELETE};
static const enum menu_action kGameActions[] = {MENU_ACTION_INFO, MENU_ACTION_DOWNLOAD_ALL,
                                                MENU_ACTION_DELETE_ALL};
// Grouping is offered only on the saves in the cloud: in a game's folder it would take away the
// folder the user is in.
static const enum menu_section kCloudSections[] = {MENU_SECTION_SORT, MENU_SECTION_GROUP};
static const enum menu_section kOtherSections[] = {MENU_SECTION_SORT};

int menu_action_count(char kind)
{
    return kind == ROW_RECORD_SAVE   ? COUNT(kSaveActions)
           : kind == ROW_RECORD_GAME ? COUNT(kGameActions)
                                     : 0;
}

enum menu_action menu_action_at(char kind, uint32_t row)
{
    if (row >= (uint32_t)menu_action_count(kind))
    {
        return MENU_ACTIONS;
    }
    return kind == ROW_RECORD_SAVE ? kSaveActions[row] : kGameActions[row];
}

int menu_section_count(enum menu_place place)
{
    return place == MENU_PLACE_CLOUD ? COUNT(kCloudSections) : COUNT(kOtherSections);
}

enum menu_section menu_section_at(enum menu_place place, uint32_t row)
{
    if (row >= (uint32_t)menu_section_count(place))
    {
        return MENU_SECTIONS;
    }
    return place == MENU_PLACE_CLOUD ? kCloudSections[row] : kOtherSections[row];
}

int menu_choice_count(enum menu_section section)
{
    return section == MENU_SECTION_SORT    ? XMB_VIEW_SORTS
           : section == MENU_SECTION_GROUP ? XMB_VIEW_GROUPS
                                           : 0;
}
