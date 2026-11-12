// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "net/HttpClient.h"

#include <functional>

class DeviceAuth;

/** One HTTP request, sent with the headers it is given; returns the status, or -1. */
typedef std::function<long(const HttpClient::Headers &)> Request;

/**
 * Sends request with headers plus the bearer token of auth, built anew for every attempt.
 * An expired token is refreshed and the request sent again once; a busy or unreachable server
 * gets a few more attempts with a doubling wait. Returns the status of the last attempt.
 */
long SendAuthorized(DeviceAuth &auth, const HttpClient::Headers &headers, const Request &request);
