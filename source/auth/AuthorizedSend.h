// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "net/HttpClient.h"

#include <functional>

class GoogleDeviceAuth;

/** One HTTP request, sent with the headers it is given; returns the status, or -1. */
typedef std::function<long(const HttpClient::Headers &)> Request;

/** Whether a request is sent again when no answer came back at all. */
enum class Retry
{
    AlsoUnanswered,
    OnlyAnswered
};

/**
 * Sends request with headers plus the bearer token of auth, built anew for every attempt.
 * An expired token is refreshed and the request sent again once; a busy server gets a few more
 * attempts with a doubling wait, and so does an unreachable one unless retry is OnlyAnswered
 * (for requests that must not be done twice). Returns the status of the last attempt.
 */
long SendAuthorized(GoogleDeviceAuth &auth, const HttpClient::Headers &headers,
                    const Request &request, Retry retry = Retry::AlsoUnanswered);
