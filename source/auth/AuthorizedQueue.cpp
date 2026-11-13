// SPDX-License-Identifier: GPL-3.0-or-later
#include "auth/AuthorizedQueue.h"

#include "auth/GoogleDeviceAuth.h"
#include "app/DebugLog.h"

#include <algorithm>
#include <utility>

#include <sys/systime.h>

namespace
{
// How long Run() sleeps between turns; it drives curl without poll(), which keeps it simple.
const unsigned int kTurnMicroseconds = 10000;
const unsigned long long kMicrosecondsPerSecond = 1000000;

unsigned long long NowMicroseconds()
{
    u64 seconds;
    u64 nanoseconds;
    sysGetCurrentTime(&seconds, &nanoseconds);
    return seconds * kMicrosecondsPerSecond + nanoseconds / 1000;
}
} // namespace

AuthorizedQueue::AuthorizedQueue(GoogleDeviceAuth &auth, unsigned int parallel)
    : m_auth(auth), m_multi(curl_multi_init()), m_slots(parallel)
{
    curl_multi_setopt(m_multi, CURLMOPT_MAX_HOST_CONNECTIONS, static_cast<long>(parallel));
    for (Slot &slot : m_slots)
    {
        slot.curl = curl_easy_init();
    }
}

AuthorizedQueue::~AuthorizedQueue()
{
    for (Slot &slot : m_slots)
    {
        if (slot.busy)
        {
            curl_multi_remove_handle(m_multi, slot.curl);
        }
        curl_slist_free_all(slot.headers);
        curl_easy_cleanup(slot.curl);
    }
    curl_multi_cleanup(m_multi);
}

void AuthorizedQueue::Add(const Prepare &prepare, const Done &done)
{
    Item item;
    item.prepare = prepare;
    item.done = done;
    m_waiting.push_back(item);
}

unsigned int AuthorizedQueue::Running() const
{
    return static_cast<unsigned int>(
        std::count_if(m_slots.begin(), m_slots.end(), [](const Slot &slot) { return slot.busy; }));
}

void AuthorizedQueue::Run(const Stop &stop)
{
    for (;;)
    {
        if (stop())
        {
            m_waiting.clear();
        }
        StartReady();
        if (Running() == 0 && m_waiting.empty())
        {
            return;
        }

        int running = 0;
        curl_multi_perform(m_multi, &running);
        int left = 0;
        while (CURLMsg *message = curl_multi_info_read(m_multi, &left))
        {
            if (message->msg != CURLMSG_DONE)
            {
                continue;
            }
            for (Slot &slot : m_slots)
            {
                if (slot.busy && slot.curl == message->easy_handle)
                {
                    long status = -1;
                    if (message->data.result == CURLE_OK)
                    {
                        curl_easy_getinfo(slot.curl, CURLINFO_RESPONSE_CODE, &status);
                    }
                    else
                    {
                        debugPrintf("AuthorizedQueue::Run: request failed: %s\n",
                                    curl_easy_strerror(message->data.result));
                    }
                    Finish(slot, status);
                    break;
                }
            }
        }
        sysUsleep(kTurnMicroseconds);
    }
}

void AuthorizedQueue::StartReady()
{
    const unsigned long long now = NowMicroseconds();
    for (Slot &slot : m_slots)
    {
        if (slot.busy)
        {
            continue;
        }
        std::deque<Item>::iterator next =
            std::find_if(m_waiting.begin(), m_waiting.end(),
                         [now](const Item &item) { return item.notBefore <= now; });
        while (next != m_waiting.end())
        {
            Item item = std::move(*next);
            m_waiting.erase(next);
            if (item.prepared || item.prepare(item.request))
            {
                item.prepared = true;
                Start(slot, std::move(item));
                break;
            }
            next = std::find_if(m_waiting.begin(), m_waiting.end(),
                                [now](const Item &waiting) { return waiting.notBefore <= now; });
        }
    }
}

void AuthorizedQueue::Start(Slot &slot, Item &&item)
{
    slot.item = std::move(item);
    ++slot.item.attempt;
    slot.reply.clear();
    curl_slist_free_all(slot.headers);
    slot.headers = NULL;
    const HttpClient::Headers headers = AddToken(slot.item.request.headers, m_auth.AccessToken());
    for (const std::string &header : headers)
    {
        slot.headers = curl_slist_append(slot.headers, header.c_str());
    }

    const QueuedRequest &request = slot.item.request;
    // A reset clears the options only; the multi handle keeps the open connections for reuse.
    curl_easy_reset(slot.curl);
    HttpClient::ApplyDefaults(slot.curl, request.url);
    curl_easy_setopt(slot.curl, CURLOPT_HTTPHEADER, slot.headers);
    curl_easy_setopt(slot.curl, CURLOPT_POSTFIELDS, request.body.data());
    curl_easy_setopt(slot.curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    if (request.method == "PUT")
    {
        curl_easy_setopt(slot.curl, CURLOPT_CUSTOMREQUEST, "PUT");
    }
    curl_easy_setopt(slot.curl, CURLOPT_WRITEFUNCTION, &HttpClient::AppendToString);
    curl_easy_setopt(slot.curl, CURLOPT_WRITEDATA, &slot.reply);
    curl_multi_add_handle(m_multi, slot.curl);
    slot.busy = true;
}

void AuthorizedQueue::Finish(Slot &slot, long status)
{
    curl_multi_remove_handle(m_multi, slot.curl);
    slot.busy = false;
    Item item = std::move(slot.item);

    if (status == 401 && !item.refreshed)
    {
        item.refreshed = true;
        if (m_auth.Refresh())
        {
            m_waiting.push_front(std::move(item));
            return;
        }
    }
    if (IsRetryable(status, slot.reply, item.request.retry) && item.attempt < kSendAttempts)
    {
        const unsigned long long jittered = AddJitter(item.wait);
        debugPrintf("AuthorizedQueue::Finish: status %ld, retrying in %llu ms\n", status,
                    jittered / 1000);
        item.notBefore = NowMicroseconds() + jittered;
        item.wait *= 2;
        m_waiting.push_front(std::move(item));
        return;
    }
    item.done(status, slot.reply);
}
