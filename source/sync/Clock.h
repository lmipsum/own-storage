// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <ctime>

/** Tells the time, so that what is left of a task can be told from how long the rest took. */
class Clock
{
  public:
    virtual ~Clock() = default;

    /** The seconds since some fixed moment. */
    virtual std::time_t Seconds() const = 0;
};

/** The console's own clock. */
class SystemClock : public Clock
{
  public:
    std::time_t Seconds() const override
    {
        return std::time(nullptr);
    }
};
