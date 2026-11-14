// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/SaveProgress.h"

#include <sstream>

SaveProgress::SaveProgress(SyncDisplay &display, std::size_t total)
    : m_display(display), m_total(total), m_done(0), m_shownPercent(0)
{
    m_display.Reset(ProgressBar::Overall);
}

void SaveProgress::Advance()
{
    ++m_done;
    std::ostringstream text;
    text << "Saves: " << m_done << " of " << m_total;
    m_display.SetText(ProgressBar::Overall, text.str());
    const unsigned int percent = static_cast<unsigned int>(m_done * 100 / m_total);
    m_display.Advance(ProgressBar::Overall, percent - m_shownPercent);
    m_shownPercent = percent;
}
