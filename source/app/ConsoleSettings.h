// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/CloudSaveText.h"

#include <string>

/**
 * How the console's settings show dates and times; day first, 24-hour, UTC where a setting
 * cannot be read.
 */
DateStyle ReadDateStyle();

/** Whether the console confirms with Circle and goes back with Cross, as set in its settings. */
bool CircleConfirms();

/** The name of the user logged in on the console; empty when it cannot be read. */
std::string CurrentUserName();
