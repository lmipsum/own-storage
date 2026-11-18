// SPDX-License-Identifier: GPL-3.0-or-later
// explore_plugin's Info page (4.88), which the calls below follow: View::PageActivate("info_list"),
// then on the page's plane "info_base_plane" a paf::PhInfoList, made with Create(rows, 0.0f), each
// row's label and value set as text parts, styled, and focused. Its RCO page has no widget but the
// plane; the list is made in code, and paf destroys it with the page.
#include "info_page.h"

#include <stddef.h>
#include <stdint.h>

#include "owns_log.h"
#include "owns_paths.h"
#include "paf.h"
#include "vshcommon.h"
#include "vtable.h"
#include "widget_text.h"

// What explore_plugin allocates for a PhInfoList.
#define INFO_LIST_BYTES 0x3b4
// A row's label and value parts, of row (index << 16).
#define ROW_LABEL_PART 0xc8
#define ROW_VALUE_PART 0xc9
// The list's label and value columns, as its style calls name them.
#define LABEL_COLUMN 0x64
#define VALUE_COLUMN 0x65
// PhWidget's layout setters and its style setters, by vtable slot, with explore_plugin's values.
#define SET_LAYOUT_POS_SLOT 0x58
#define SET_LAYOUT_SIZE_SLOT 0x5c
#define SET_STYLE_INT_SLOT 0xa8
#define SET_STYLE_FLAG_SLOT 0xb8
#define SET_STYLE_METRIC_SLOT 0xf4
#define SET_FOCUS_SLOT 0x130
#define LAYOUT_POS_X_METRIC 0x1da
#define LAYOUT_POS_Y_METRIC 0x1db
#define LAYOUT_POS_ANCHOR 6
#define LAYOUT_SIZE_METRIC 0x1dc
#define LIST_STYLE 0x12
#define LIST_STYLE_VALUE 0x10
#define COLUMN_FLAG_STYLE 0x2f
#define COLUMN_MARGIN_MODE 0
#define COLUMN_MARGIN_METRIC 0x14
#define COLUMN_WIDTH_MODE 5
#define LABEL_WIDTH_METRIC 0x1e0
#define VALUE_WIDTH_METRIC 0x1e1
#define FOCUS_MODE 2

/** Sets a row's label and value. */
static void set_row(void *list, int index, const info_row *row)
{
    const unsigned short *label = menu_text(&row->label);
    if (label != NULL)
    {
        widget_set_text(list, (index << 16) | ROW_LABEL_PART, label);
    }
    const int part = (index << 16) | ROW_VALUE_PART;
    if (row->value_units != NULL)
    {
        widget_set_text(list, part, row->value_units);
    }
    else if (row->value != NULL)
    {
        widget_set_utf8(list, part, row->value);
    }
}

/** Places and styles the list as explore_plugin's Info does (its vec4 arguments are zero). */
static void style_list(void *list)
{
    typedef int (*set_layout)(void *widget, int x, int y, int z, uint64_t vec_xy, uint64_t vec_zw);
    typedef int (*set_style_int)(void *widget, int style, int value);
    typedef int (*set_style_flag)(void *widget, int part, int style, int on);
    typedef int (*set_style_metric)(void *widget, int part, int mode, int metric, float value);
    ((set_layout)virtual_function(list, SET_LAYOUT_SIZE_SLOT))(list, 0, LAYOUT_SIZE_METRIC, 0, 0,
                                                               0);
    ((set_layout)virtual_function(list, SET_LAYOUT_POS_SLOT))(
        list, LAYOUT_POS_X_METRIC, LAYOUT_POS_Y_METRIC, LAYOUT_POS_ANCHOR, 0, 0);
    const set_style_metric metric = (set_style_metric)virtual_function(list, SET_STYLE_METRIC_SLOT);
    metric(list, LABEL_COLUMN, COLUMN_MARGIN_MODE, COLUMN_MARGIN_METRIC, 0.0f);
    metric(list, VALUE_COLUMN, COLUMN_MARGIN_MODE, COLUMN_MARGIN_METRIC, 0.0f);
    ((set_style_int)virtual_function(list, SET_STYLE_INT_SLOT))(list, LIST_STYLE, LIST_STYLE_VALUE);
    const set_style_flag flag = (set_style_flag)virtual_function(list, SET_STYLE_FLAG_SLOT);
    flag(list, LABEL_COLUMN, COLUMN_FLAG_STYLE, 1);
    flag(list, VALUE_COLUMN, COLUMN_FLAG_STYLE, 1);
    metric(list, LABEL_COLUMN, COLUMN_WIDTH_MODE, LABEL_WIDTH_METRIC, 0.0f);
    metric(list, VALUE_COLUMN, COLUMN_WIDTH_MODE, VALUE_WIDTH_METRIC, 0.0f);
}

int info_page_open(int view, const info_row *rows, int count, enum xmb_backdrop backdrop)
{
    // As explore_plugin's Info opens: the XMB's bar and columns go first.
    xmb_backdrop_set(backdrop);
    View_Activate(view);
    // Only an RCO's first page is built as the view loads; this one must be opened first.
    owns_log_value("info page opened",
                   (uint32_t)(uintptr_t)View_PageOpen(view, OWNS_INFO_PAGE, 0, 0, -1, 0, 0));
    View_PageActivateByName(view, OWNS_INFO_PAGE);
    void *plane = View_FindWidget(view, OWNS_INFO_PLANE);
    void *list = plane != NULL ? paf_new(INFO_LIST_BYTES) : NULL;
    if (list == NULL)
    {
        View_PageClose(view, OWNS_INFO_PAGE);
        xmb_backdrop_set(XMB_BACKDROP_OFF);
        return 0;
    }
    PhInfoList_ctor(list, plane, NULL);
    PhInfoList_Create(list, count, 0.0f);
    for (int index = 0; index < count; ++index)
    {
        set_row(list, index, &rows[index]);
    }
    style_list(list);
    typedef void (*set_focus)(void *widget, void *event, unsigned int mode);
    ((set_focus)virtual_function(list, SET_FOCUS_SLOT))(list, NULL, FOCUS_MODE);
    // As explore_plugin's Info shows O's hint; the page's close hides it again.
    ShowButtonNavigation_Back(1);
    return 1;
}
