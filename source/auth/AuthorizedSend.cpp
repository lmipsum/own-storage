// SPDX-License-Identifier: GPL-3.0-or-later
#include "auth/AuthorizedSend.h"

#include "auth/GoogleDeviceAuth.h"
#include "app/DebugLog.h"

#include <sys/systime.h>

namespace
{
const int kAttempts = 5;
const unsigned long long kFirstWaitMicroseconds = 1000000;

/** True for a status that may pass on a later attempt. */
bool IsTransient(long status)
{
    // Google asks clients to retry rate limits and server errors with exponential backoff.
    return status == -1 || status == 429 || status >= 500;
}

HttpClient::Headers AddToken(const HttpClient::Headers &headers, const std::string &token)
{
    HttpClient::Headers all(headers);
    all.push_back("Authorization: Bearer " + token);
    return all;
}
} // namespace

long SendAuthorized(GoogleDeviceAuth &auth, const HttpClient::Headers &headers,
                    const Request &request)
{
    bool refreshed = false;
    unsigned long long wait = kFirstWaitMicroseconds;
    long status = -1;
    for (int attempt = 1; attempt <= kAttempts; ++attempt)
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
        if (!IsTransient(status) || attempt == kAttempts)
        {
            return status;
        }
        debugPrintf("Request failed with %ld, retrying in %llu ms\n", status, wait / 1000);
        sysUsleep(wait);
        wait *= 2;
    }
    return status;
}
