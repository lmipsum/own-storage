// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/Screen.h"

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

/** The pad buttons a menu takes. */
enum class PadButton
{
    Up,
    Down,
    /** Ten rows up. */
    PageUp,
    /** Ten rows down. */
    PageDown,
    Select,
    Back,
    /** Asks to remove the row's item, where a menu allows it. */
    Remove
};

/** What the user did in a menu that allows removing. */
struct MenuChoice
{
    /** The row; Menu::kNone when the user went back or the app is closing. */
    int row;
    /** True when the row was picked to be removed, not selected. */
    bool remove;
};

/**
 * Menus and messages on the screen. The app thread shows them and waits for the user's choice;
 * the main thread only hands over the pad's buttons as they are pressed.
 */
class Menu
{
  public:
    /** The choice when the user goes back, or the app closes. */
    static constexpr int kNone = -1;

    /**
     * @param screen where the menu is drawn
     * @param quit set when the app is closing, which ends any wait for a choice
     */
    Menu(Screen &screen, const std::atomic<int> &quit);
    Menu(const Menu &) = delete;
    Menu &operator=(const Menu &) = delete;

    /**
     * Shows a menu and waits until the user picks a row or goes back. App thread only.
     * @param heading the list's heading; empty for a short menu under the logo
     * @param rows the rows to pick from
     * @param hint the buttons the menu takes
     * @param cursor the row the cursor starts on
     * @return the row picked; kNone when the user went back or the app is closing
     */
    int Choose(const std::string &heading, const std::vector<std::string> &rows,
               const std::string &hint, std::size_t cursor);
    /**
     * Shows a menu and waits until the user picks a row, picks one to remove, or goes back. App
     * thread only.
     * @param heading the list's heading
     * @param rows the rows to pick from
     * @param hint the buttons the menu takes
     * @param cursor the row the cursor starts on
     * @return what the user did
     */
    MenuChoice ChooseOrRemove(const std::string &heading, const std::vector<std::string> &rows,
                              const std::string &hint, std::size_t cursor);
    /** Shows a line about what the app is doing; it stays under the next menu too. */
    void ShowMessage(const std::string &text);
    /** Whether the user pressed Back since the last call; for waits that the user may end. */
    bool TakeBack();
    /** Hands over a press of a pad button; main thread only. */
    void Press(PadButton button);

  private:
    /** Shows a menu and waits for the user; Remove ends the wait only when removable. */
    MenuChoice Wait(const std::string &heading, const std::vector<std::string> &rows,
                    const std::string &hint, std::size_t cursor, bool removable);
    /** Shows the menu as it is now. */
    void Show();

    Screen &m_screen;
    const std::atomic<int> &m_quit;
    // The button pressed and not yet taken; kNoPress when none.
    std::atomic<int> m_pressed;
    ScreenView m_view;
};
