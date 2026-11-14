// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/SaveBrowser.h"

#include <string>

/** The order of a date's parts, as the console's settings choose it. */
enum class DateOrder
{
    YearMonthDay,
    DayMonthYear,
    MonthDayYear
};

/** How the console shows dates and times. */
struct DateStyle
{
    DateOrder order;
    /** True for a 12-hour clock with AM and PM, false for a 24-hour one. */
    bool clock12;
    /** The console's local time minus UTC, in minutes, summer time included. */
    int offsetMinutes;
};

/** A size the way the console shows a save's: "3787 KB", or "36 MB" from 10000 KB on. */
std::string ShownSize(unsigned long long bytes);

/**
 * Drive's time of a change in local time, the way the console shows dates.
 * @param modified UTC, such as "2026-11-14T10:56:21.672Z"
 * @param style the console's date and time settings
 * @return such as "28/2/2024 11:33 PM", as the console writes it without leading zeros; the time
 * as it is when it is not in Drive's form
 */
std::string ShownTime(const std::string &modified, const DateStyle &style);

/** What a save's status means to the user. */
const char *ShownStatus(SaveStatus status);

/** What loading a save came to, as the user reads it. */
std::string ShownResult(const std::string &name, LoadResult result);

/** What deleting a save from the cloud came to, as the user reads it. */
std::string ShownResult(const std::string &name, RemoveResult result);
