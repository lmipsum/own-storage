// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/CloudSaveText.h"

#include <iomanip>
#include <sstream>

namespace
{
const unsigned long long kBytesPerKilobyte = 1024;
// From this many kilobytes on, the console counts a save's size in megabytes; the line and the
// rounding down are read from two sizes the console showed, not from a documented rule.
const unsigned long long kKilobytesShownUpTo = 10000;
// Drive's "YYYY-MM-DDTHH:MM", then seconds and the zone: where each field starts, and its length.
const std::size_t kYearAt = 0;
const std::size_t kYearLength = 4;
const std::size_t kMonthAt = 5;
const std::size_t kDayAt = 8;
const std::size_t kHourAt = 11;
const std::size_t kMinuteAt = 14;
const std::size_t kFieldLength = 2;
const std::size_t kShortestTime = kMinuteAt + kFieldLength;
// Later years would not fit the minutes counted on the console's 32-bit long.
const long kLastYear = 3000;
const int kMinutesPerHour = 60;
const int kMinutesPerDay = 24 * kMinutesPerHour;
const int kHalfDayHours = 12;

/** Days since 1970-01-01 of a date of the Gregorian calendar (H. Hinnant's days_from_civil). */
long DaysFromCivil(long year, int month, int day)
{
    year -= month <= 2 ? 1 : 0;
    const long era = (year >= 0 ? year : year - 399) / 400;
    const long yearOfEra = year - era * 400;
    const long dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const long dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + dayOfEra - 719468;
}

/** The date of a day since 1970-01-01 (H. Hinnant's civil_from_days). */
void CivilFromDays(long days, long &year, int &month, int &day)
{
    days += 719468;
    const long era = (days >= 0 ? days : days - 146096) / 146097;
    const long dayOfEra = days - era * 146097;
    const long yearOfEra =
        (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const long dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const long monthIndex = (5 * dayOfYear + 2) / 153;
    day = static_cast<int>(dayOfYear - (153 * monthIndex + 2) / 5 + 1);
    month = static_cast<int>(monthIndex < 10 ? monthIndex + 3 : monthIndex - 9);
    year = yearOfEra + era * 400 + (month <= 2 ? 1 : 0);
}

/**
 * A field of a time in Drive's form.
 * @param time the time
 * @param at where the field starts
 * @param length its digits
 * @param lowest the lowest value it may have
 * @param highest the highest value it may have
 * @param number receives the value
 * @return false when the field is not all digits or out of range
 */
bool ReadField(const std::string &time, std::size_t at, std::size_t length, long lowest,
               long highest, long &number)
{
    const std::string text = time.substr(at, length);
    if (text.size() != length || text.find_first_not_of("0123456789") != std::string::npos)
    {
        return false;
    }
    // Counted by hand: the console's C++ library may lack std::stol.
    number = 0;
    for (const char digit : text)
    {
        number = number * 10 + (digit - '0');
    }
    return number >= lowest && number <= highest;
}
} // namespace

std::string ShownSize(unsigned long long bytes)
{
    const unsigned long long kilobytes = bytes / kBytesPerKilobyte;
    std::ostringstream text;
    if (kilobytes < kKilobytesShownUpTo)
    {
        text << kilobytes << " KB";
    }
    else
    {
        text << kilobytes / kBytesPerKilobyte << " MB";
    }
    return text.str();
}

std::string ShownTime(const std::string &modified, const DateStyle &style)
{
    long year;
    long month;
    long day;
    long hour;
    long minute;
    if (modified.size() < kShortestTime || modified[kMonthAt - 1] != '-' ||
        modified[kDayAt - 1] != '-' || modified[kHourAt - 1] != 'T' ||
        modified[kMinuteAt - 1] != ':' ||
        !ReadField(modified, kYearAt, kYearLength, 1970, kLastYear, year) ||
        !ReadField(modified, kMonthAt, kFieldLength, 1, 12, month) ||
        !ReadField(modified, kDayAt, kFieldLength, 1, 31, day) ||
        !ReadField(modified, kHourAt, kFieldLength, 0, 23, hour) ||
        !ReadField(modified, kMinuteAt, kFieldLength, 0, 59, minute))
    {
        return modified;
    }
    const long utcMinutes =
        DaysFromCivil(year, static_cast<int>(month), static_cast<int>(day)) * kMinutesPerDay +
        hour * kMinutesPerHour + minute;
    const long local = utcMinutes + style.offsetMinutes;
    // Floored, so a time before 1970 still lands on the right day.
    const long days = local >= 0 ? local / kMinutesPerDay : (local + 1) / kMinutesPerDay - 1;
    const long minuteOfDay = local - days * kMinutesPerDay;
    long localYear;
    int localMonth;
    int localDay;
    CivilFromDays(days, localYear, localMonth, localDay);

    std::ostringstream text;
    text << std::setfill('0');
    switch (style.order)
    {
    case DateOrder::YearMonthDay:
        text << localYear << '/' << localMonth << '/' << localDay;
        break;
    case DateOrder::DayMonthYear:
        text << localDay << '/' << localMonth << '/' << localYear;
        break;
    case DateOrder::MonthDayYear:
        text << localMonth << '/' << localDay << '/' << localYear;
        break;
    }
    const long localHour = minuteOfDay / kMinutesPerHour;
    const long localMinute = minuteOfDay % kMinutesPerHour;
    if (style.clock12)
    {
        const long shownHour =
            localHour % kHalfDayHours == 0 ? kHalfDayHours : localHour % kHalfDayHours;
        text << ' ' << shownHour << ':' << std::setw(2) << localMinute
             << (localHour < kHalfDayHours ? " AM" : " PM");
    }
    else
    {
        text << ' ' << localHour << ':' << std::setw(2) << localMinute;
    }
    return text.str();
}

const char *ShownStatus(SaveStatus status)
{
    switch (status)
    {
    case SaveStatus::Missing:
        return "not on this PS3";
    case SaveStatus::Same:
        return "on this PS3";
    default:
        return "different on this PS3";
    }
}

std::string ShownResult(const std::string &name, LoadResult result)
{
    switch (result)
    {
    case LoadResult::Loaded:
        return name + " is loaded.";
    case LoadResult::LoadedNotResigned:
        return name + " is loaded as it was: it could not be made this user's.";
    case LoadResult::AlreadyThere:
        return name + " is on this PS3 already.";
    case LoadResult::Kept:
        return name + " stays as it is on this PS3.";
    default:
        return name + " could not be loaded.";
    }
}

std::string ShownResult(const std::string &name, RemoveResult result)
{
    switch (result)
    {
    case RemoveResult::Removed:
        return name + " is in the Drive trash now.";
    case RemoveResult::OnConsole:
        return name + " is on this PS3; delete it there first, or the next sync sends it up again.";
    case RemoveResult::Kept:
        return name + " stays in the cloud.";
    default:
        return name + " could not be deleted from the cloud.";
    }
}
