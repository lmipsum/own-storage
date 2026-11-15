// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/SyncDialog.h"

#include <sys/systime.h>
#include <sysutil/msg.h>

namespace
{
// The bars show a sync as well as a single save downloaded or an import, each of which a quit or
// a switch-off would leave half done.
const char kProgressText[] = "Own Storage\nDo not quit or switch off the PS3.";
const msgType kProgressType =
    static_cast<msgType>(MSG_DIALOG_MUTE_ON | MSG_DIALOG_DOUBLE_PROGRESSBAR);
// Only Yes or No ends a question: closing it without an answer would leave the saves in doubt.
const msgType kQuestionType = static_cast<msgType>(MSG_DIALOG_NORMAL | MSG_DIALOG_BTN_TYPE_YESNO |
                                                   MSG_DIALOG_DISABLE_CANCEL_ON);
const msgType kErrorType = static_cast<msgType>(MSG_DIALOG_ERROR | MSG_DIALOG_BTN_TYPE_OK);
const msgType kInfoType = static_cast<msgType>(MSG_DIALOG_NORMAL | MSG_DIALOG_BTN_TYPE_OK);
// Without buttons: only the back button closes it, while the app waits for something else.
const msgType kWaitingType = MSG_DIALOG_NORMAL;
// No button has closed the question or message yet.
const int kNoButton = MSG_DIALOG_BTN_INVALID;
const unsigned int kPollMicroseconds = 100000;

/** The system's index of a progress bar. */
u32 Index(ProgressBar bar)
{
    return bar == ProgressBar::Overall ? MSG_PROGRESSBAR_INDEX0 : MSG_PROGRESSBAR_INDEX1;
}

/** Any button on the progress bars stops the sync; closing them from the app does not. */
void OnProgressButton(msgButton button, void *cancelled)
{
    if (button != MSG_DIALOG_BTN_NONE)
    {
        static_cast<std::atomic<bool> *>(cancelled)->store(true);
    }
}

/**
 * Keeps the button that closed a question or a message. A dialog the app closes reports no
 * button, and may report it only once the next one is open, which must not end that one's wait.
 */
void OnAnswerButton(msgButton button, void *pressed)
{
    if (button != MSG_DIALOG_BTN_NONE)
    {
        static_cast<std::atomic<int> *>(pressed)->store(button);
    }
}
} // namespace

SyncDialog::SyncDialog(const std::atomic<int> &quit)
    : m_quit(quit), m_cancelled(false), m_button(kNoButton), m_progressShown(false)
{
}

void SyncDialog::Open()
{
    m_cancelled = false;
    ShowProgress();
}

void SyncDialog::Close()
{
    msgDialogAbort();
    m_progressShown = false;
}

void SyncDialog::ShowError(const std::string &message)
{
    ShowNotice(kErrorType, message);
}

void SyncDialog::ShowInfo(const std::string &message)
{
    ShowNotice(kInfoType, message);
}

bool SyncDialog::ShowWaiting(const std::string &message)
{
    Close();
    m_button = kNoButton;
    return OpenWhenFree(kWaitingType, message);
}

bool SyncDialog::WaitingClosed() const
{
    return m_button != kNoButton;
}

void SyncDialog::ShowNotice(int type, const std::string &message)
{
    Close();
    m_button = kNoButton;
    if (OpenWhenFree(type, message))
    {
        // OK and the back button both close it.
        WaitForButton();
    }
}

void SyncDialog::SetText(ProgressBar bar, const std::string &text)
{
    ShowProgress();
    msgDialogProgressBarSetMsg(Index(bar), text.c_str());
}

void SyncDialog::Reset(ProgressBar bar)
{
    ShowProgress();
    msgDialogProgressBarReset(Index(bar));
}

void SyncDialog::Advance(ProgressBar bar, unsigned int points)
{
    ShowProgress();
    msgDialogProgressBarInc(Index(bar), points);
}

bool SyncDialog::Cancelled() const
{
    return m_cancelled || m_quit != 0;
}

Answer SyncDialog::Ask(const std::string &question)
{
    Close();
    m_button = kNoButton;
    if (!OpenWhenFree(kQuestionType, question))
    {
        return Answer::None;
    }
    switch (WaitForButton())
    {
    case MSG_DIALOG_BTN_YES:
        return Answer::Yes;
    case MSG_DIALOG_BTN_NO:
        return Answer::No;
    default:
        return Answer::None;
    }
}

void SyncDialog::ShowProgress()
{
    if (m_progressShown)
    {
        return;
    }
    while (msgDialogOpen2(kProgressType, kProgressText, OnProgressButton, &m_cancelled, nullptr) !=
           0)
    {
        if (m_quit != 0)
        {
            return;
        }
        sysUsleep(kPollMicroseconds);
    }
    m_progressShown = true;
}

bool SyncDialog::OpenWhenFree(int type, const std::string &text)
{
    while (msgDialogOpen2(static_cast<msgType>(type), text.c_str(), OnAnswerButton, &m_button,
                          nullptr) != 0)
    {
        if (m_quit != 0)
        {
            return false;
        }
        sysUsleep(kPollMicroseconds);
    }
    return true;
}

int SyncDialog::WaitForButton()
{
    while (m_quit == 0)
    {
        const int button = m_button;
        if (button != kNoButton)
        {
            return button;
        }
        sysUsleep(kPollMicroseconds);
    }
    return MSG_DIALOG_BTN_NONE;
}
