// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/Screen.h"

namespace
{
// A plain dark screen with light text, as the system's own dialogs have.
const Uint8 kBackground[] = {24, 24, 24};
const SDL_Color kTextColor = {230, 230, 230, 0};
// Where the line goes, in percent of the screen's height: under the system's dialogs.
const int kMessageTopPercent = 86;
} // namespace

Screen::Screen(SDL_Surface *surface, TTF_Font *font)
    : m_surface(surface), m_font(font), m_lock(SDL_CreateMutex()), m_changed(true)
{
}

Screen::~Screen()
{
    SDL_DestroyMutex(m_lock);
}

void Screen::ShowMessage(const std::string &text)
{
    SDL_LockMutex(m_lock);
    m_message = text;
    m_changed = true;
    SDL_UnlockMutex(m_lock);
}

void Screen::Draw()
{
    std::string message;
    SDL_LockMutex(m_lock);
    const bool changed = m_changed;
    message = m_message;
    m_changed = false;
    SDL_UnlockMutex(m_lock);
    if (!changed)
    {
        return;
    }
    SDL_FillRect(m_surface, nullptr,
                 SDL_MapRGB(m_surface->format, kBackground[0], kBackground[1], kBackground[2]));
    if (message.empty())
    {
        return;
    }
    SDL_Surface *rendered = TTF_RenderUTF8_Blended(m_font, message.c_str(), kTextColor);
    if (rendered == nullptr)
    {
        return;
    }
    SDL_Rect where = {static_cast<Sint16>((m_surface->w - rendered->w) / 2),
                      static_cast<Sint16>(kMessageTopPercent * m_surface->h / 100), 0, 0};
    SDL_BlitSurface(rendered, nullptr, m_surface, &where);
    SDL_FreeSurface(rendered);
}
