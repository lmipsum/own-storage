// SPDX-License-Identifier: GPL-3.0-or-later
#include "auth/AuthorizedSend.h"

#include "auth/GoogleDeviceAuth.h"
#include "app/DebugLog.h"

#include <sys/systime.h>

bool IsRetryable(long status, Retry retry)
{
    // Google asks clients to retry rate limits and server errors with exponential backoff. A
    // server error or a lost answer may come after the request was done, though.
    return status == 429 || (retry == Retry::AnyTransient && (status == -1 || status >= 500));
}

HttpClient::Headers AddToken(const HttpClient::Headers &headers, const std::string &token)
{
    HttpClient::Headers all(headers);
    all.push_back("Authorization: Bearer " + token);
    return all;
}

long SendAuthorized(GoogleDeviceAuth &auth, const HttpClient::Headers &headers,
                    const Request &request, Retry retry)
{
    bool refreshed = false;
    unsigned long long wait = kFirstRetryWaitMicroseconds;
    long status = -1;
    for (int attempt = 1; attempt <= kSendAttempts; ++attempt)
    {
        status = request(AddToken(headers, auth.AccessToken()));
        if (status == 401 && !refreshed)
        {
            refreshed = true;
            if (!auth.Refresh())
            {
                return status;
            }
            continue;
        }
        if (!IsRetryable(status, retry) || attempt == kSendAttempts)
        {
            return status;
        }
        debugPrintf("Request failed with %ld, retrying in %llu ms\n", status, wait / 1000);
        sysUsleep(wait);
        wait *= 2;
    }
    return status;
}
