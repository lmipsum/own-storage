// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include <cstddef>
#include <string>
#include <vector>

/** What the screen shows over the background. */
struct ScreenView
{
    /** A line about what the app is doing, or what it did last; may be empty. */
    std::string message;
    /** The heading of a list; empty for a menu that fits under the logo. */
    std::string heading;
    /** The menu's rows; empty when no menu is shown. */
    std::vector<std::string> rows;
    /** The row the cursor is on. */
    std::size_t cursor;
    /** The buttons the menu takes. */
    std::string hint;
};

/**
 * The app's screen: the background, and over it a message and a menu. Any thread may say what to
 * show; only the main thread draws, as SDL is not thread-safe.
 */
class Screen
{
  public:
    /**
     * @param surface the video surface
     * @param background drawn first on every frame that changes
     * @param font the text's font
     */
    Screen(SDL_Surface *surface, SDL_Surface *background, TTF_Font *font);
    ~Screen();
    Screen(const Screen &) = delete;
    Screen &operator=(const Screen &) = delete;

    /** Shows this from the next frame on; any thread. */
    void Show(const ScreenView &view);
    /** Redraws when the view changed; main thread only. */
    void Draw();

  private:
    /** Lays the light panel over the background, which a list would otherwise cover. */
    void DrawPanel();
    /** Draws a line of text with its left edge at x, or centred when x is below zero. */
    void DrawText(const std::string &text, int x, int y, SDL_Color color);
    /**
     * Draws the rows of a menu from top down, as many as fit above bottom, keeping the cursor's
     * row among them.
     */
    void DrawRows(const ScreenView &view, int x, int top, int bottom);

    SDL_Surface *m_surface;
    SDL_Surface *m_background;
    SDL_Surface *m_panel;
    TTF_Font *m_font;
    SDL_mutex *m_lock;
    ScreenView m_view;
    bool m_changed;
    /** The first of the menu's rows on screen; main thread only. */
    std::size_t m_firstRow;
};
