// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/TransferBar.h"

#include "app/DebugLog.h"

#include <sysutil/msg.h>

namespace
{
// How full the bar is for the file moving now, in percent.
double shownPercent = 0.0;

/**
 * Moves the bar on when the file moved at least a percent further since it last did.
 * @param direction "UP" or "DOWN", for the debug log
 * @param now the bytes moved so far
 * @param total the file's bytes
 */
void ShowProgress(const char *direction, double now, double total)
{
    const double percent = now / total * 100;
    if (percent - shownPercent > 1)
    {
        msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX1, percent - shownPercent);
        debugPrintf("%s: %f of %f\r\n", direction, now, total);
        shownPercent = percent;
    }
}

/** curl's progress callback; a file moves one way, so only one of the totals is set. */
int ReportProgress(void *, double downTotal, double downNow, double upTotal, double upNow)
{
    if (downTotal > 0.0)
    {
        ShowProgress("DOWN", downNow, downTotal);
    }
    if (upTotal > 0.0)
    {
        ShowProgress("UP", upNow, upTotal);
    }
    return 0;
}

/** Empties the bar and names the file that starts to move. */
void BeginTransfer(const std::string &path)
{
    shownPercent = 0.0;
    msgDialogProgressBarReset(MSG_PROGRESSBAR_INDEX1);
    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX1, path.c_str());
}

/** Fills the bar for a file that moved in one go. */
void EndTransfer()
{
    msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX1, 100);
}
} // namespace

TransferDisplay MakeTransferBar()
{
    return TransferDisplay{BeginTransfer, ReportProgress, EndTransfer};
}
