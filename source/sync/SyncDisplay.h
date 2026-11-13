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

/** How the user answered a question. */
enum class Answer
{
    Yes,
    No,
    /** The app is closing, so no answer came. */
    None
};

/** What the user sees of the sync, how the user stops it, and what the user decides. */
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
    /**
     * Asks a yes-or-no question in place of the progress bars, which come back with the next
     * change to them.
     * @param question the question, with what each answer does
     * @return the answer, once given
     */
    virtual Answer Ask(const std::string &question) = 0;
};
