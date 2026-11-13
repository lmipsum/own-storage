// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/CloudSaveMenu.h"

#include "app/CloudSaveText.h"

#include <sstream>
#include <string>
#include <vector>

namespace
{
const char kGamesHeading[] = "Saves in the cloud";
const char kGamesHint[] = "X: open    O: back";
const char kSavesHint[] = "X: load to this PS3    O: back";

/** Lists a game's saves and loads the one the user picks, until the user goes back. */
void BrowseGame(Menu &menu, SyncDialog &dialog, SaveBrowser &browser, const CloudGame &game)
{
    std::size_t cursor = 0;
    std::string result;
    while (true)
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
        const int chosen = menu.Choose(game.label, rows, kSavesHint, cursor);
        if (chosen == Menu::kNone)
        {
            return;
        }
        cursor = static_cast<std::size_t>(chosen);
        const std::string &name = game.saves[cursor];
        result = ShownResult(name, browser.Load(name));
        dialog.Close();
    }
}
} // namespace

bool BrowseCloudSaves(Menu &menu, SyncDialog &dialog, SaveBrowser &browser)
{
    menu.ShowMessage("Listing the saves in the cloud...");
    if (!browser.Refresh())
    {
        return false;
    }
    const std::vector<CloudGame> games = browser.Games();
    if (games.empty())
    {
        menu.ShowMessage("There are no saves in the cloud yet.");
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
    std::size_t cursor = 0;
    while (true)
    {
        menu.ShowMessage(std::string());
        const int chosen = menu.Choose(kGamesHeading, rows, kGamesHint, cursor);
        if (chosen == Menu::kNone)
        {
            return true;
        }
        cursor = static_cast<std::size_t>(chosen);
        BrowseGame(menu, dialog, browser, games[cursor]);
    }
}
