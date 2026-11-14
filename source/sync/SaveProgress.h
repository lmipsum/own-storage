// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/SyncDisplay.h"

#include <cstddef>

/** How far the upper progress bar is through a number of saves. */
class SaveProgress
{
  public:
    /**
     * Empties the bar.
     * @param display where the bar is
     * @param total the saves to go through
     */
    SaveProgress(SyncDisplay &display, std::size_t total);
    /** Counts one more save done and moves the bar. */
    void Advance();

  private:
    SyncDisplay &m_display;
    std::size_t m_total;
    std::size_t m_done;
    unsigned int m_shownPercent;
};
