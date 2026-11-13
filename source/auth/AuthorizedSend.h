// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "net/HttpClient.h"

#include <functional>
#include <string>

class GoogleDeviceAuth;

/**
 * One HTTP request, sent with the headers it is given; writes the response body to reply and
 * returns the status, or -1.
 */
typedef std::function<long(const HttpClient::Headers &headers, std::string &reply)> Request;

/** Which failures a request is sent again after. */
enum class Retry
{
    /** No answer, a rate limit or a server error: for requests that may be done twice. */
    AnyTransient,
    /** Only a rate limit, which Google answers without doing the request: for creates. */
    OnlyRefused
};

/** How often a request is sent at most, and the wait before the first repeat (it doubles). */
const int kSendAttempts = 5;
const unsigned long long kFirstRetryWaitMicroseconds = 1000000;
/** The most a retry waits beyond its doubling wait, picked at random. */
const unsigned long long kMaxRetryJitterMicroseconds = 1000000;

/**
 * Adds a random part to a retry wait, so that requests that failed together, such as on a rate
 * limit, are not sent again together.
 * @param wait the doubling wait
 * @return the wait plus up to kMaxRetryJitterMicroseconds
 */
unsigned long long AddJitter(unsigned long long wait);

/**
 * The first reason in a Google API error reply.
 * @param reply the response body
 * @return the reason, such as "rateLimitExceeded"; "" when the reply is not such an error
 */
std::string ReadErrorReason(const std::string &reply);

/**
 * Whether a failed request may pass on a later attempt.
 * @param status the HTTP status, or -1 when no answer came
 * @param reply the response body
 * @param retry which failures the request may be sent again after
 * @return true when it is worth sending the request again
 */
bool IsRetryable(long status, const std::string &reply, Retry retry);

/** The headers with an Authorization header for token added. */
HttpClient::Headers AddToken(const HttpClient::Headers &headers, const std::string &token);

/**
 * Sends request with headers plus the bearer token of auth, built anew for every attempt.
 * An expired token is refreshed and the request sent again once; the failures retry names get
 * a few more attempts with a doubling wait.
 * @param reply receives the response body of the last attempt
 * @return the status of the last attempt, or -1 when no answer came
 */
long SendAuthorized(GoogleDeviceAuth &auth, const HttpClient::Headers &headers,
                    const Request &request, std::string &reply, Retry retry = Retry::AnyTransient);
