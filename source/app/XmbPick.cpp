// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/XmbPick.h"

#include "app/CloudSaveText.h"
#include "app/XmbLists.h"
#include "sync/GameName.h"

namespace
{
/** Whether an action starts with a prefix, and the rest of it then. */
bool TakePrefix(const std::string &action, const char *prefix, std::string &rest)
{
    const std::string start(prefix);
    if (action.compare(0, start.size(), start) != 0)
    {
        return false;
    }
    rest = action.substr(start.size());
    return !rest.empty();
}

/** Tells a caller that asks whether the XMB's lists need an update. */
void SetChanged(bool *changed, bool value)
{
    if (changed != nullptr)
    {
        *changed = value;
    }
}
} // namespace

bool ReadXmbSaveAction(const std::string &action, XmbSaveAction &what, std::string &name)
{
    if (TakePrefix(action, xmbSave, name))
    {
        what = XmbSaveAction::Download;
        return true;
    }
    if (TakePrefix(action, xmbInformation, name))
    {
        what = XmbSaveAction::Information;
        return true;
    }
    if (TakePrefix(action, xmbDelete, name))
    {
        what = XmbSaveAction::Delete;
        return true;
    }
    return false;
}

Notice DownloadXmbSave(SaveBrowser &browser, const std::string &name, bool *changed)
{
    // The XMB lists what the cloud had when the app last ran, and the gone save the next update
    // leaves out.
    if (!browser.Has(name))
    {
        SetChanged(changed, true);
        return XmbSaveGone(name);
    }
    // The browser asks before it replaces a different copy, and loads a missing one as it is.
    const LoadResult result = browser.Load(name);
    SetChanged(changed, result == LoadResult::Loaded || result == LoadResult::LoadedNotResigned);
    return ShownResult(name, result);
}

std::string XmbSaveInformation(SaveBrowser &browser, const std::string &name)
{
    if (!browser.Has(name))
    {
        return std::string();
    }
    const SaveTexts &texts = browser.Save(name).texts;
    const std::string head = GameLabel(TitleId(name), texts.title) + ":\n";
    std::string body = texts.subtitle.empty() ? name : texts.subtitle;
    const std::string detail = texts.detail.substr(0, texts.detail.find_last_not_of('\n') + 1);
    if (!detail.empty())
    {
        body += "\n\n" + detail;
    }
    return FitInDialog(head, body, std::string());
}

Notice DeleteXmbSave(SaveBrowser &browser, const std::string &name, bool *changed)
{
    if (!browser.Has(name))
    {
        SetChanged(changed, true);
        return XmbSaveGone(name);
    }
    // The browser asks before it deletes, and keeps a save the console has too.
    const RemoveResult result = browser.Remove(name);
    SetChanged(changed, result == RemoveResult::Removed);
    return ShownResult(name, result);
}

Notice XmbSaveGone(const std::string &name)
{
    return Result(name + " is no longer in the cloud.");
}
