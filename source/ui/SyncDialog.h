// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/SyncDisplay.h"

#include <atomic>
#include <string>

/**
 * The sync in the system's message dialog: two progress bars, and in their place a question or
 * an error when one comes. The system reports the buttons on the main thread, while it runs its
 * callbacks, and the main thread closes the dialog when the app ends; everything else is for the
 * sync thread.
 */
class SyncDialog : public SyncDisplay
{
  public:
    /** @param quit set when the app is closing, which ends any wait for the user */
    explicit SyncDialog(const std::atomic<int> &quit);
    SyncDialog(const SyncDialog &) = delete;
    SyncDialog &operator=(const SyncDialog &) = delete;

    /** Shows the progress bars, unless they are shown. */
    void ShowProgress();
    /** Takes the dialog away. */
    void Close();
    /** Shows an error in place of the progress bars and waits until the user closes it. */
    void ShowError(const std::string &message);

    /** Brings the progress bars back first when a question or an error took their place. */
    void SetText(ProgressBar bar, const std::string &text) override;
    void Reset(ProgressBar bar) override;
    void Advance(ProgressBar bar, unsigned int points) override;
    bool Cancelled() const override;
    Answer Ask(const std::string &question) override;

  private:
    /**
     * Opens a dialog once the system lets it, which it does not while the last one is closing.
     * @param type the dialog's msgType
     * @param text what it says
     * @return false when the app closes first
     */
    bool OpenWhenFree(int type, const std::string &text);
    /**
     * Waits until a button closes the question or error.
     * @return the button; MSG_DIALOG_BTN_NONE when the app closes first
     */
    int WaitForButton();

    const std::atomic<int> &m_quit;
    /** Set when the user closes the progress bars, which stops the sync. */
    std::atomic<bool> m_cancelled;
    /** The button that closed the last question or error. */
    std::atomic<int> m_button;
    std::atomic<bool> m_progressShown;
};
