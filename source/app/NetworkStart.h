// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <ppu-types.h>

#include <atomic>

/**
 * The system's dialog that checks the network connection and, when there is none, tells the
 * user why in the system's own words. The main thread starts its library and hands over the
 * system's events about it; the app thread opens it.
 */
class NetworkStart
{
  public:
    /** @param quit set when the app is closing, which ends a wait for the dialog */
    explicit NetworkStart(const std::atomic<int> &quit);
    NetworkStart(const NetworkStart &) = delete;
    NetworkStart &operator=(const NetworkStart &) = delete;

    /** Loads and starts the dialog's library; main thread, after the network is up. */
    void Start();
    /** Stops and unloads the library again; main thread, once the app thread has ended. */
    void Stop();
    /** Hands over a system event; main thread only. */
    void OnSystemEvent(u64 status);
    /**
     * Shows the dialog and waits until it closes; app thread only. Without the library the app
     * goes on, and a request that fails says so in its own words.
     * @return false when the console has no connection, or the app is closing
     */
    bool Connect();

  private:
    /** Waits until an event has come; false when the app is closing first. */
    bool WaitFor(const std::atomic<bool> &event) const;

    const std::atomic<int> &m_quit;
    bool m_loaded;
    bool m_started;
    std::atomic<bool> m_finished;
    std::atomic<bool> m_unloaded;
};
