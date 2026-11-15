// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <rsx/rsx.h>

#include <cstdint>

/**
 * The app's screen: a plain dark frame, flipped on every vertical blank. The system draws its
 * dialogs over the app's frames, so they show only while the app keeps flipping.
 */
class BlankScreen
{
  public:
    BlankScreen() = default;
    BlankScreen(const BlankScreen &) = delete;
    BlankScreen &operator=(const BlankScreen &) = delete;
    ~BlankScreen();

    /**
     * Takes the display over, at its resolution.
     * @return false when the RSX or the display cannot be set up
     */
    bool Open();
    /** Waits for the last frame to show, then shows the other one. */
    void Flip();

  private:
    static const int kBuffers = 2;

    /** Fills a frame of the display's size with the dark colour and gives it to the display. */
    bool MakeBuffer(int id, std::uint16_t width, std::uint16_t height);

    void *m_host = nullptr;
    gcmContextData *m_context = nullptr;
    std::uint32_t *m_buffers[kBuffers] = {nullptr, nullptr};
    int m_current = 0;
    /** Whether a flip has been queued, which the RSX must finish before it stops. */
    bool m_flipping = false;
};
