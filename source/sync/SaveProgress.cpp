// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/SaveProgress.h"

#include <sstream>

namespace
{
// The while the cloud takes to answer for any save, in the bytes that move in that while: about
// a second of an upload.
const unsigned long long kSaveAnswerBytes = 1024 * 1024;
// Until then the saves done tell too little of the rest.
const std::time_t kFirstEstimateSeconds = 10;
const std::time_t kMinuteSeconds = 60;
} // namespace

SaveProgress::SaveProgress(SyncDisplay &display, const Clock &clock, std::size_t total,
                           unsigned long long totalBytes)
    : m_display(display), m_clock(clock), m_started(clock.Seconds()), m_total(total), m_done(0),
      m_totalWeight(totalBytes + total * kSaveAnswerBytes), m_doneWeight(0), m_shownPercent(0)
{
    m_display.Reset(ProgressBar::Overall);
}

void SaveProgress::Advance(unsigned long long bytes)
{
    ++m_done;
    m_doneWeight += Weight(bytes);
    std::ostringstream text;
    text << "Saves: " << m_done << " of " << m_total;
    const std::time_t elapsed = m_clock.Seconds() - m_started;
    if (m_done < m_total && elapsed >= kFirstEstimateSeconds && m_doneWeight < m_totalWeight)
    {
        const unsigned long long leftWeight = m_totalWeight - m_doneWeight;
        const std::time_t left = static_cast<std::time_t>(static_cast<double>(elapsed) *
                                                          static_cast<double>(leftWeight) /
                                                          static_cast<double>(m_doneWeight));
        text << ", " << TimeLeft(left);
    }
    m_display.SetText(ProgressBar::Overall, text.str());
    const unsigned int percent = static_cast<unsigned int>(m_done * 100 / m_total);
    m_display.Advance(ProgressBar::Overall, percent - m_shownPercent);
    m_shownPercent = percent;
}

std::string SaveProgress::TimeLeft(std::time_t seconds)
{
    if (seconds < kMinuteSeconds)
    {
        return "less than a minute left";
    }
    std::ostringstream text;
    text << "about " << (seconds + kMinuteSeconds / 2) / kMinuteSeconds << " min left";
    return text.str();
}

unsigned long long SaveProgress::Weight(unsigned long long bytes)
{
    return bytes + kSaveAnswerBytes;
}
