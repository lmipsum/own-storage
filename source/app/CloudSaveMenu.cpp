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
const char kGamesHint[] = "X: open    O: back";
// The font has no button symbols, so the triangle is named.
const char kSavesHint[] = "X: load to this PS3    Triangle: delete from the cloud    O: back";

/**
 * Lists a game's saves and loads or deletes the one the user picks, until the user goes back or
 * the game has no save left in the cloud.
 * @return whether a save was deleted
 */
bool BrowseGame(Menu &menu, SyncDialog &dialog, SaveBrowser &browser, CloudGame game)
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
            rows.push_back(name + "    " + ShownTime(save.modified) + "    " +
                           ShownSize(save.size) + "    " + ShownStatus(browser.Status(name)));
        }
        menu.ShowMessage(result);
        const MenuChoice chosen = menu.ChooseOrRemove(game.label, rows, kSavesHint, cursor);
        if (chosen.row == Menu::kNone)
        {
            return deleted;
        }
        cursor = static_cast<std::size_t>(chosen.row);
        const std::string name = game.saves[cursor];
        if (!chosen.remove)
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

bool BrowseCloudSaves(Menu &menu, SyncDialog &dialog, SaveBrowser &browser)
{
    menu.ShowMessage("Listing the saves in the cloud...");
    if (!browser.Refresh())
    {
        return false;
    }
    menu.ShowMessage(std::string());
    std::size_t cursor = 0;
    bool deleted = false;
    while (true)
    {
        // Listed anew each time, as a save may have been deleted.
        const std::vector<CloudGame> games = browser.Games();
        if (games.empty())
        {
            // A deletion that emptied the cloud keeps its own message.
            if (!deleted)
            {
                menu.ShowMessage("There are no saves in the cloud.");
            }
            return true;
        }
        std::vector<std::string> rows;
        for (const CloudGame &game : games)
        {
            std::ostringstream row;
            row << game.label << "    " << game.saves.size()
                << (game.saves.size() == 1 ? " save" : " saves");
            rows.push_back(row.str());
        }
        const int chosen = menu.Choose(kGamesHeading, rows, kGamesHint, cursor);
        if (chosen == Menu::kNone)
        {
            return true;
        }
        cursor = static_cast<std::size_t>(chosen);
        deleted = BrowseGame(menu, dialog, browser, games[cursor]);
    }
}
