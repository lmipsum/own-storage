// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/Screen.h"

#include <algorithm>

namespace
{
const SDL_Color kTextColor = {57, 57, 57, 0};
const SDL_Color kCursorColor = {255, 255, 255, 0};
// The cursor row's bar, in the colour of the app's icon.
const Uint8 kCursorBar[] = {84, 46, 140};
// How opaque the light panel under a list is, out of 255.
const Uint8 kPanelOpacity = 210;
// Where things go, in percent of the screen's height or width.
const int kListLeftPercent = 8;
const int kHeadingTopPercent = 6;
const int kListTopPercent = 14;
const int kListBottomPercent = 86;
const int kHintTopPercent = 92;
const int kRowSpacing = 6;
} // namespace

Screen::Screen(SDL_Surface *surface, SDL_Surface *background, TTF_Font *font)
    : m_surface(surface), m_background(background), m_panel(nullptr), m_font(font),
      m_lock(SDL_CreateMutex()), m_view{std::string(), std::string(), {}, 0, std::string()},
      m_changed(true), m_firstRow(0)
{
    // One line of the panel, blitted down the screen: a whole screen of it would take megabytes.
    // Without an alpha channel of its own, the surface's alpha blends all of it the same.
    m_panel = SDL_CreateRGBSurface(SDL_SWSURFACE, surface->w, 1, 32, 0x00ff0000, 0x0000ff00,
                                   0x000000ff, 0);
    if (m_panel != nullptr)
    {
        SDL_FillRect(m_panel, nullptr, SDL_MapRGB(m_panel->format, 255, 255, 255));
        SDL_SetAlpha(m_panel, SDL_SRCALPHA, kPanelOpacity);
    }
}

Screen::~Screen()
{
    SDL_FreeSurface(m_panel);
    SDL_DestroyMutex(m_lock);
}

void Screen::Show(const ScreenView &view)
{
    SDL_LockMutex(m_lock);
    m_view = view;
    m_changed = true;
    SDL_UnlockMutex(m_lock);
}

void Screen::Draw()
{
    ScreenView view;
    SDL_LockMutex(m_lock);
    const bool changed = m_changed;
    if (changed)
    {
        view = m_view;
        m_changed = false;
    }
    SDL_UnlockMutex(m_lock);
    if (!changed)
    {
        return;
    }

    const int height = m_surface->h;
    const int width = m_surface->w;
    const int rowHeight = TTF_FontLineSkip(m_font) + kRowSpacing;
    const int hintTop = kHintTopPercent * height / 100;
    const int messageTop = hintTop - rowHeight;
    SDL_BlitSurface(m_background, nullptr, m_surface, nullptr);
    if (view.heading.empty())
    {
        // A short menu stands on the message, so it stays clear of the logo however many rows it
        // has.
        const int menuTop =
            messageTop - static_cast<int>(view.rows.size()) * rowHeight - kRowSpacing;
        DrawRows(view, -1, menuTop, messageTop);
        DrawText(view.message, -1, messageTop, kTextColor);
    }
    else
    {
        DrawPanel();
        const int left = kListLeftPercent * width / 100;
        DrawText(view.heading, left, kHeadingTopPercent * height / 100, kTextColor);
        DrawRows(view, left, kListTopPercent * height / 100, kListBottomPercent * height / 100);
        DrawText(view.message, left, messageTop, kTextColor);
    }
    DrawText(view.hint, -1, hintTop, kTextColor);
}

void Screen::DrawPanel()
{
    if (m_panel == nullptr)
    {
        return;
    }
    for (int y = 0; y < m_surface->h; ++y)
    {
        SDL_Rect where = {0, static_cast<Sint16>(y), 0, 0};
        SDL_BlitSurface(m_panel, nullptr, m_surface, &where);
    }
}

void Screen::DrawRows(const ScreenView &view, int x, int top, int bottom)
{
    if (view.rows.empty())
    {
        m_firstRow = 0;
        return;
    }
    const int rowHeight = TTF_FontLineSkip(m_font) + kRowSpacing;
    const std::size_t fitting = static_cast<std::size_t>(std::max(1, (bottom - top) / rowHeight));
    // The rows shown move only when the cursor would leave them.
    if (view.cursor < m_firstRow || m_firstRow >= view.rows.size())
    {
        m_firstRow = view.cursor;
    }
    else if (view.cursor >= m_firstRow + fitting)
    {
        m_firstRow = view.cursor - fitting + 1;
    }
    // Rows of a list shown before may have left it scrolled further than this one needs.
    m_firstRow = std::min(m_firstRow, view.rows.size() - std::min(fitting, view.rows.size()));
    const std::size_t end = std::min(view.rows.size(), m_firstRow + fitting);
    for (std::size_t row = m_firstRow; row < end; ++row)
    {
        const int y = top + static_cast<int>(row - m_firstRow) * rowHeight;
        const bool selected = row == view.cursor;
        if (selected)
        {
            SDL_Rect bar = {0, static_cast<Sint16>(y - kRowSpacing / 2),
                            static_cast<Uint16>(m_surface->w), static_cast<Uint16>(rowHeight)};
            if (x >= 0)
            {
                bar.x = static_cast<Sint16>(x - kRowSpacing);
                bar.w = static_cast<Uint16>(m_surface->w - 2 * x + 2 * kRowSpacing);
            }
            SDL_FillRect(
                m_surface, &bar,
                SDL_MapRGB(m_surface->format, kCursorBar[0], kCursorBar[1], kCursorBar[2]));
        }
        DrawText(view.rows[row], x, y, selected ? kCursorColor : kTextColor);
    }
}

void Screen::DrawText(const std::string &text, int x, int y, SDL_Color color)
{
    if (text.empty())
    {
        return;
    }
    // Game titles are UTF-8, such as "Heavenly Sword™".
    SDL_Surface *rendered = TTF_RenderUTF8_Blended(m_font, text.c_str(), color);
    if (rendered == nullptr)
    {
        return;
    }
    SDL_Rect where = {static_cast<Sint16>(x < 0 ? (m_surface->w - rendered->w) / 2 : x),
                      static_cast<Sint16>(y), 0, 0};
    SDL_BlitSurface(rendered, nullptr, m_surface, &where);
    SDL_FreeSurface(rendered);
}
