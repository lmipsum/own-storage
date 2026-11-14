// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/CloudSaveMenu.h"

#include "app/CloudSaveText.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace
{
const char kGamesHeading[] = "Saves in the cloud";
const char kGamesHint[] = "X: open    Square: import from USB    O: back";
// The font has no button symbols, so the triangle is named.
const char kSavesHint[] = "X: load to this PS3    Triangle: delete from the cloud    O: back";

/**
 * Lists a game's saves and loads or deletes the one the user picks, until the user goes back or
 * the game has no save left in the cloud.
 * @return whether a save was deleted
 */
bool BrowseGame(Menu &menu, SyncDialog &dialog, SaveBrowser &browser, CloudGame game,
                const DateStyle &dates)
{
    bool deleted = false;
    std::size_t cursor = 0;
    std::string result;
    while (!game.saves.empty())
    {
        menu.ShowMessage("Comparing with this PS3...");
        std::vector<std::string> rows;
        for (const std::string &name : game.saves)
        {
            const CloudSave &save = browser.Save(name);
            rows.push_back(game.name + " | " + name + " | " + ShownTime(save.modified, dates) +
                           " " + ShownSize(save.size) + " | " + ShownStatus(browser.Status(name)));
        }
        menu.ShowMessage(result);
        const MenuChoice chosen =
            menu.ChooseAction(game.label, rows, kSavesHint, cursor, {PadButton::Remove});
        if (chosen.button == PadButton::Back)
        {
            return deleted;
        }
        cursor = static_cast<std::size_t>(chosen.row);
        const std::string name = game.saves[cursor];
        if (chosen.button == PadButton::Select)
        {
            result = ShownResult(name, browser.Load(name));
        }
        else
        {
            const RemoveResult removed = browser.Remove(name);
            result = ShownResult(name, removed);
            if (removed == RemoveResult::Removed)
            {
                deleted = true;
                game.saves.erase(game.saves.begin() + chosen.row);
                // The cursor stays where it was, on the save that took the deleted one's place.
                cursor = std::min(cursor, game.saves.empty() ? 0 : game.saves.size() - 1);
            }
        }
        dialog.Close();
    }
    menu.ShowMessage(result);
    return deleted;
}
} // namespace

bool BrowseCloudSaves(Menu &menu, SyncDialog &dialog, SaveBrowser &browser, const DateStyle &dates,
                      const std::function<std::string()> &importSaves)
{
    menu.ShowMessage("Listing the saves in the cloud...");
    if (!browser.Refresh())
    {
        return false;
    }
    menu.ShowMessage(std::string());
    std::size_t cursor = 0;
    // A deletion or an import says what came of it, which an empty list does not hide.
    bool reported = false;
    while (true)
    {
        // Listed anew each time, as a save may have been deleted or imported.
        const std::vector<CloudGame> games = browser.Games();
        if (games.empty() && !reported)
        {
            menu.ShowMessage("There are no saves in the cloud.");
        }
        std::vector<std::string> rows;
        for (const CloudGame &game : games)
        {
            std::ostringstream row;
            row << game.label << "    " << game.saves.size()
                << (game.saves.size() == 1 ? " save" : " saves");
            rows.push_back(row.str());
        }
        // The list is shown even when empty, so that saves can be imported into it.
        const MenuChoice chosen =
            menu.ChooseAction(kGamesHeading, rows, kGamesHint, cursor, {PadButton::Import});
        if (chosen.button == PadButton::Back)
        {
            return true;
        }
        if (chosen.button == PadButton::Import)
        {
            const std::string result = importSaves();
            dialog.Close();
            if (!browser.Refresh())
            {
                return false;
            }
            menu.ShowMessage(result);
            reported = true;
            continue;
        }
        cursor = static_cast<std::size_t>(chosen.row);
        reported = BrowseGame(menu, dialog, browser, games[cursor], dates);
    }
}
