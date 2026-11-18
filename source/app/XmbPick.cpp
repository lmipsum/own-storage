// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/XmbPick.h"

#include "app/CloudSaveText.h"
#include "app/XmbLists.h"
#include "sync/GameName.h"

#include <sstream>

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

/** A game in the cloud by its ID; false when the cloud has no save of it any more. */
bool FindGame(const SaveBrowser &browser, const std::string &titleId, CloudGame &found)
{
    for (const CloudGame &game : browser.Games())
    {
        if (game.titleId == titleId)
        {
            found = game;
            return true;
        }
    }
    return false;
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
    const std::string detail = ShownDetail(texts.detail);
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
    // The browser keeps a save the console has too.
    const RemoveResult result = browser.Remove(name);
    SetChanged(changed, result == RemoveResult::Removed);
    return ShownResult(name, result);
}

Notice XmbSaveGone(const std::string &name)
{
    return Result(name + " is no longer in the cloud.");
}

bool ReadXmbGameAction(const std::string &action, XmbGameAction &what, std::string &titleId)
{
    if (TakePrefix(action, xmbDownloadAll, titleId))
    {
        what = XmbGameAction::DownloadAll;
        return true;
    }
    if (TakePrefix(action, xmbDeleteAll, titleId))
    {
        what = XmbGameAction::DeleteAll;
        return true;
    }
    return false;
}

Notice DownloadXmbGame(SaveBrowser &browser, const std::string &titleId, bool *changed)
{
    CloudGame game;
    if (!FindGame(browser, titleId, game))
    {
        SetChanged(changed, true);
        return XmbSaveGone(titleId);
    }
    std::size_t onConsole = 0;
    bool loaded = false;
    bool notResigned = false;
    for (const std::string &name : game.saves)
    {
        const LoadResult result = browser.Load(name);
        const bool loadedNow =
            result == LoadResult::Loaded || result == LoadResult::LoadedNotResigned;
        loaded = loaded || loadedNow;
        notResigned = notResigned || result == LoadResult::LoadedNotResigned;
        onConsole += loadedNow || result == LoadResult::AlreadyThere ? 1 : 0;
    }
    SetChanged(changed, loaded);
    std::ostringstream text;
    text << "Downloaded " << onConsole << " of " << SaveCount(game.saves.size()) << " of "
         << game.label << ".";
    // As for a single save: the game may refuse a save that is not the user's.
    if (notResigned)
    {
        text << " Some could not be made this user's.";
    }
    return Result(text.str());
}

Notice DeleteXmbGame(SaveBrowser &browser, const std::string &titleId, bool *changed)
{
    CloudGame game;
    if (!FindGame(browser, titleId, game))
    {
        SetChanged(changed, true);
        return XmbSaveGone(titleId);
    }
    std::size_t removed = 0;
    std::size_t kept = 0;
    for (const std::string &name : game.saves)
    {
        switch (browser.Remove(name))
        {
        case RemoveResult::Removed:
            ++removed;
            break;
        case RemoveResult::OnConsole:
            ++kept;
            break;
        default:
            break;
        }
    }
    SetChanged(changed, removed > 0);
    std::ostringstream text;
    text << SaveCount(removed) << " of " << game.label << " moved to the Drive trash";
    if (kept > 0)
    {
        text << "; " << kept << " on this PS3 kept";
    }
    text << ".";
    return Result(text.str());
}
