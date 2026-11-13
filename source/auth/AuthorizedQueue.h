// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "auth/AuthorizedSend.h"
#include "net/HttpClient.h"

#include <curl/curl.h>

#include <deque>
#include <functional>
#include <string>
#include <vector>

class GoogleDeviceAuth;

/** A request for AuthorizedQueue: everything but the bearer token. */
struct QueuedRequest
{
    /** "POST" or "PUT"; the body goes with either. */
    std::string method;
    std::string url;
    HttpClient::Headers headers;
    std::string body;
    Retry retry = Retry::AnyTransient;
};

/**
 * Sends many authorized requests at once, each on its own connection, by the rules of
 * SendAuthorized: a refreshed token after a 401, and doubling waits for a busy server.
 * Everything, callbacks included, runs on the thread that calls Run(), so they need no locks.
 */
class AuthorizedQueue
{
  public:
    /** Builds the request when its turn comes; false when nothing needs to be sent. */
    typedef std::function<bool(QueuedRequest &request)> Prepare;
    /** Takes the result: the HTTP status, or -1 when no answer came, and the reply body. */
    typedef std::function<void(long status, const std::string &reply)> Done;
    /** Asked between requests; true lets the running ones finish and drops the rest. */
    typedef std::function<bool()> Stop;

    /**
     * @param auth the sign-in whose token every request carries
     * @param parallel how many requests are sent at once at most
     */
    AuthorizedQueue(GoogleDeviceAuth &auth, unsigned int parallel);
    ~AuthorizedQueue();

    /** Queues a request; nothing is sent before Run(). */
    void Add(const Prepare &prepare, const Done &done);
    /** Sends the queued requests, at most parallel at a time, until all are done or stopped. */
    void Run(const Stop &stop);
    /** How many requests are being sent right now. */
    unsigned int Running() const;

  private:
    struct Item
    {
        Prepare prepare;
        Done done;
        QueuedRequest request;
        bool prepared = false;
        int attempt = 0;
        bool refreshed = false;
        unsigned long long notBefore = 0;
        unsigned long long wait = kFirstRetryWaitMicroseconds;
    };
    struct Slot
    {
        CURL *curl = NULL;
        curl_slist *headers = NULL;
        std::string reply;
        Item item;
        bool busy = false;
    };

    AuthorizedQueue(const AuthorizedQueue &) = delete;
    AuthorizedQueue &operator=(const AuthorizedQueue &) = delete;

    /** Fills idle slots with the queued items that may be sent now. */
    void StartReady();
    /** Sends item on slot, with a fresh token. */
    void Start(Slot &slot, Item &&item);
    /** Hands a finished request to its callback, or queues it again by the retry rules. */
    void Finish(Slot &slot, long status);

    GoogleDeviceAuth &m_auth;
    CURLM *m_multi;
    std::vector<Slot> m_slots;
    std::deque<Item> m_waiting;
};
