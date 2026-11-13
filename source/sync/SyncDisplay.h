// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

/** The two progress bars the sync shows. */
enum class ProgressBar
{
    /** How far the current step is. */
    Overall,
    /** What is moving right now. */
    Current
};

/** What the user sees of the sync, and how the user stops it. */
class SyncDisplay
{
  public:
    virtual ~SyncDisplay() = default;

    /** Sets the text above a bar. */
    virtual void SetText(ProgressBar bar, const std::string &text) = 0;
    /** Empties a bar. */
    virtual void Reset(ProgressBar bar) = 0;
    /** Fills a bar further by the given percentage points. */
    virtual void Advance(ProgressBar bar, unsigned int points) = 0;
    /** Whether the user has asked the sync to stop. */
    virtual bool Cancelled() const = 0;
};
