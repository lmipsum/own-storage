// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include <string>

/**
 * The app's screen behind the system's dialogs: a plain background with a line about what the app
 * is doing. Any thread may say what to show; only the main thread draws, as SDL is not
 * thread-safe.
 */
class Screen
{
  public:
    /**
     * @param surface the video surface
     * @param font the text's font
     */
    Screen(SDL_Surface *surface, TTF_Font *font);
    ~Screen();
    Screen(const Screen &) = delete;
    Screen &operator=(const Screen &) = delete;

    /** Shows a line about what the app is doing from the next frame on; any thread. */
    void ShowMessage(const std::string &text);
    /** Redraws when the line changed; main thread only. */
    void Draw();

  private:
    SDL_Surface *m_surface;
    TTF_Font *m_font;
    SDL_mutex *m_lock;
    std::string m_message;
    bool m_changed;
};
