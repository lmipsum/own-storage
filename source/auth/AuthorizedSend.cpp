// SPDX-License-Identifier: GPL-3.0-or-later
#include "auth/AuthorizedSend.h"

#include "auth/GoogleDeviceAuth.h"
#include "app/DebugLog.h"
#include "json/Json.h"

#include <sys/systime.h>

std::string ReadErrorReason(const std::string &reply)
{
    return Json::Parse(reply)["error"]["errors"][0u]["reason"].Str();
}

bool IsRetryable(long status, const std::string &reply, Retry retry)
{
    // Google asks clients to retry rate limits and server errors with exponential backoff. A
    // rate limit comes as 429, or as 403 with one of these reasons; a server error or a lost
    // answer may come after the request was done, though.
    if (status == 429)
    {
        return true;
    }
    if (status == 403)
    {
        const std::string reason = ReadErrorReason(reply);
        return reason == "rateLimitExceeded" || reason == "userRateLimitExceeded";
    }
    return retry == Retry::AnyTransient && (status == -1 || status >= 500);
}

HttpClient::Headers AddToken(const HttpClient::Headers &headers, const std::string &token)
{
    HttpClient::Headers all(headers);
    all.push_back("Authorization: Bearer " + token);
    return all;
}

long SendAuthorized(GoogleDeviceAuth &auth, const HttpClient::Headers &headers,
                    const Request &request, std::string &reply, Retry retry)
{
    bool refreshed = false;
    unsigned long long wait = kFirstRetryWaitMicroseconds;
    long status = -1;
    for (int attempt = 1; attempt <= kSendAttempts; ++attempt)
    {
        status = request(AddToken(headers, auth.AccessToken()), reply);
        if (status == 401 && !refreshed)
        {
            refreshed = true;
            if (!auth.Refresh())
            {
                return status;
            }
            continue;
        }
        if (!IsRetryable(status, reply, retry) || attempt == kSendAttempts)
        {
            return status;
        }
        debugPrintf("SendAuthorized: status %ld, retrying in %llu ms\n", status, wait / 1000);
        sysUsleep(wait);
        wait *= 2;
    }
    return status;
}
