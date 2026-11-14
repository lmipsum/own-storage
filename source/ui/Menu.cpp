// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/Menu.h"

#include <sys/systime.h>

#include <algorithm>

namespace
{
const int kNoPress = -1;
// Short enough that a menu follows the pad without a felt delay.
const unsigned int kPollMicroseconds = 20000;
// How many rows a page press moves.
const std::size_t kPageRows = 10;

/**
 * Where a button moves the cursor; up from the first row wraps to the last and back.
 * @param cursor the row the cursor is on
 * @param button the button pressed
 * @param last the last row
 * @return the row the cursor goes to
 */
std::size_t MoveCursor(std::size_t cursor, PadButton button, std::size_t last)
{
    switch (button)
    {
    case PadButton::Up:
        return cursor == 0 ? last : cursor - 1;
    case PadButton::Down:
        return cursor == last ? 0 : cursor + 1;
    case PadButton::PageUp:
        return cursor < kPageRows ? 0 : cursor - kPageRows;
    case PadButton::PageDown:
        return last - cursor < kPageRows ? last : cursor + kPageRows;
    default:
        return cursor;
    }
}
} // namespace

Menu::Menu(Screen &screen, const std::atomic<int> &quit, const ButtonNames &buttons)
    : m_screen(screen), m_quit(quit), m_pressed(kNoPress),
      m_view{std::string(), std::string(), {}, 0, std::string(), std::string()}, m_buttons(buttons)
{
}

int Menu::Choose(const std::string &heading, const std::vector<std::string> &rows,
                 const std::string &hint, std::size_t cursor)
{
    return ChooseAction(heading, rows, hint, cursor, {}).row;
}

MenuChoice Menu::ChooseAction(const std::string &heading, const std::vector<std::string> &rows,
                              const std::string &hint, std::size_t cursor,
                              const std::vector<PadButton> &actions)
{
    m_view.heading = heading;
    m_view.rows = rows;
    m_view.hint = hint;
    m_view.cursor = cursor < rows.size() ? cursor : 0;
    // A press made before the menu was shown was meant for something else.
    m_pressed = kNoPress;
    Show();
    MenuChoice choice{kNone, PadButton::Back};
    while (m_quit == 0)
    {
        const int pressed = m_pressed.exchange(kNoPress);
        if (pressed == kNoPress)
        {
            sysUsleep(kPollMicroseconds);
            continue;
        }
        const PadButton button = static_cast<PadButton>(pressed);
        const int row = rows.empty() ? kNone : static_cast<int>(m_view.cursor);
        const bool allowed = std::find(actions.begin(), actions.end(), button) != actions.end();
        // Select and Remove act on a row; Import needs none.
        if ((button == PadButton::Select || (button == PadButton::Remove && allowed)) &&
            row != kNone)
        {
            choice = MenuChoice{row, button};
            break;
        }
        if (button == PadButton::Import && allowed)
        {
            choice = MenuChoice{row, button};
            break;
        }
        if (button == PadButton::Back)
        {
            break;
        }
        const std::size_t moved =
            MoveCursor(m_view.cursor, button, rows.empty() ? 0 : rows.size() - 1);
        if (moved != m_view.cursor)
        {
            m_view.cursor = moved;
            Show();
        }
    }
    m_view.rows.clear();
    m_view.heading.clear();
    m_view.hint.clear();
    Show();
    return choice;
}

void Menu::ShowMessage(const std::string &text)
{
    m_view.message = text;
    Show();
}

void Menu::ShowCaption(const std::string &text)
{
    m_view.caption = text;
    Show();
}

const ButtonNames &Menu::Buttons() const
{
    return m_buttons;
}

bool Menu::TakeBack()
{
    int back = static_cast<int>(PadButton::Back);
    return m_pressed.compare_exchange_strong(back, kNoPress);
}

void Menu::Press(PadButton button)
{
    m_pressed = static_cast<int>(button);
}

void Menu::Show()
{
    m_screen.Show(m_view);
}
