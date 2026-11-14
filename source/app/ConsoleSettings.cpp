// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ConsoleSettings.h"

#include <sysutil/sysutil.h>

namespace
{
// The settings' values, as the system documents them.
const s32 kDateYearMonthDay = 0;
const s32 kDateMonthDayYear = 2;
const s32 kClock12 = 0;
const int kSummerTimeMinutes = 60;
const s32 kEnterButtonCircle = 0;

/** A setting's value; fallback when it cannot be read. */
s32 Setting(s32 id, s32 fallback)
{
    s32 value = 0;
    return sysUtilGetSystemParamInt(id, &value) == 0 ? value : fallback;
}
} // namespace

DateStyle ReadDateStyle()
{
    const s32 date = Setting(SYSUTIL_SYSTEMPARAM_ID_DATE_FORMAT, -1);
    const DateOrder order = date == kDateYearMonthDay   ? DateOrder::YearMonthDay
                            : date == kDateMonthDayYear ? DateOrder::MonthDayYear
                                                        : DateOrder::DayMonthYear;
    const bool clock12 = Setting(SYSUTIL_SYSTEMPARAM_ID_TIME_FORMAT, -1) == kClock12;
    // The time zone is the offset from UTC in minutes; summer time adds an hour on top.
    const int offset =
        Setting(SYSUTIL_SYSTEMPARAM_ID_TIMEZONE, 0) +
        (Setting(SYSUTIL_SYSTEMPARAM_ID_SUMMERTIME, 0) != 0 ? kSummerTimeMinutes : 0);
    return DateStyle{order, clock12, offset};
}

bool CircleConfirms()
{
    return Setting(SYSUTIL_SYSTEMPARAM_ID_ENTER_BUTTON_ASSIGN, -1) == kEnterButtonCircle;
}

std::string CurrentUserName()
{
    char name[SYSUTIL_SYSTEMPARAM_CURRENT_USERNAME_SIZE + 1] = {};
    return sysUtilGetSystemParamString(SYSUTIL_SYSTEMPARAM_ID_CURRENT_USERNAME, name,
                                       SYSUTIL_SYSTEMPARAM_CURRENT_USERNAME_SIZE) == 0
               ? std::string(name)
               : std::string();
}
