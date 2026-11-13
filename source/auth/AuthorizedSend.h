// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "net/HttpClient.h"

#include <functional>

class GoogleDeviceAuth;

/** One HTTP request, sent with the headers it is given; returns the status, or -1. */
typedef std::function<long(const HttpClient::Headers &)> Request;

/** Which failures a request is sent again after. */
enum class Retry
{
    /** No answer, a rate limit or a server error: for requests that may be done twice. */
    AnyTransient,
    /** Only a rate limit, which the server answers without doing the request: for creates. */
    OnlyRefused
};

/** How often a request is sent at most, and the wait before the first repeat (it doubles). */
const int kSendAttempts = 5;
const unsigned long long kFirstRetryWaitMicroseconds = 1000000;

/** True for a status that may pass on a later attempt, as retry allows. */
bool IsRetryable(long status, Retry retry);

/** The headers with an Authorization header for token added. */
HttpClient::Headers AddToken(const HttpClient::Headers &headers, const std::string &token);

/**
 * Sends request with headers plus the bearer token of auth, built anew for every attempt.
 * An expired token is refreshed and the request sent again once; the failures retry names get
 * a few more attempts with a doubling wait. Returns the status of the last attempt.
 */
long SendAuthorized(GoogleDeviceAuth &auth, const HttpClient::Headers &headers,
                    const Request &request, Retry retry = Retry::AnyTransient);
