// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/XmbPick.h"

#include "app/CloudSaveText.h"
#include "sync/GameName.h"

std::string PickXmbSave(SaveBrowser &browser, SyncDisplay &display, const std::string &name)
{
    // The XMB lists what the cloud had when the app last ran.
    if (!browser.Has(name))
    {
        return name + " is no longer in the cloud.";
    }
    switch (browser.Status(name))
    {
    case SaveStatus::Same:
        return ShownResult(name, LoadResult::AlreadyThere);
    case SaveStatus::Different:
        // The browser asks before it replaces the console's copy.
        return ShownResult(name, browser.Load(name));
    default:
        break;
    }
    // The question shows what the XMB's row has no room for: what the game says of the save.
    const SaveTexts &texts = browser.Save(name).texts;
    const std::string head = GameLabel(TitleId(name), texts.title) + ":\n";
    const std::string question = name + " is not on this PS3. Download it?";
    const std::string detail = texts.detail.substr(0, texts.detail.find_last_not_of('\n') + 1);
    const Answer answer = display.Ask(
        detail.empty() ? head + question : FitInDialog(head, detail, "\n\n" + question));
    if (answer == Answer::Yes)
    {
        return ShownResult(name, browser.Load(name));
    }
    if (answer == Answer::None)
    {
        // The app is closing.
        return ShownResult(name, LoadResult::Kept);
    }
    // The browser asks before it deletes.
    return ShownResult(name, browser.Remove(name));
}
