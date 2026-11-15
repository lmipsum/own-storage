// SPDX-License-Identifier: GPL-3.0-or-later
#include "xmb_return.h"

#include "owns_log.h"
#include "paf.h"

void xmb_return_focus(void)
{
    const int explore = View_Find("explore_plugin");
    if (explore != 0)
    {
        View_Activate(explore);
        owns_log_value("explore list page activated",
                       (uint32_t)View_PageActivateByName(explore, "expage_explore_list"));
        return;
    }
    const int xmb = View_Find("xmb_plugin");
    if (xmb != 0)
    {
        View_Activate(xmb);
        owns_log_value("xmb page activated", (uint32_t)View_PageActivateByName(xmb, "page_xmb"));
    }
}
