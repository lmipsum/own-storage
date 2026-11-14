// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/Clock.h"
#include "sync/SyncDisplay.h"

#include <cstddef>
#include <ctime>
#include <string>

/**
 * How far the upper progress bar is through a number of saves, and about how long the rest will
 * take. The time left is told from the time the saves done took, by their size: a save takes the
 * time its bytes take to move, and a while more that the cloud takes to answer for any save. An
 * upload is sized by its files, a download by its archive, which is only a little larger.
 */
class SaveProgress
{
  public:
    /**
     * Empties the bar.
     * @param display where the bar is
     * @param clock tells how long the saves took
     * @param total the saves to go through
     * @param totalBytes their sizes together
     */
    SaveProgress(SyncDisplay &display, const Clock &clock, std::size_t total,
                 unsigned long long totalBytes);

    /**
     * Counts one more save done and moves the bar.
     * @param bytes the save's size
     */
    void Advance(unsigned long long bytes);

    /**
     * What is left, as the user reads it.
     * @param seconds the seconds left
     * @return such as "about 3 min left"
     */
    static std::string TimeLeft(std::time_t seconds);

  private:
    /** How much a save of the given size weighs in the time the saves take. */
    static unsigned long long Weight(unsigned long long bytes);

    SyncDisplay &m_display;
    const Clock &m_clock;
    std::time_t m_started;
    std::size_t m_total;
    std::size_t m_done;
    unsigned long long m_totalWeight;
    unsigned long long m_doneWeight;
    unsigned int m_shownPercent;
};
