// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <curl/curl.h>

#include <string>
#include <vector>

/**
 * HTTPS requests over one reused curl handle, so later requests to the same host skip the TLS
 * handshake. Server certificates are checked against the CA bundle shipped with the app.
 *
 * Every request returns the HTTP status, or kNoAnswer, kNotSent or kLocalError when no response
 * arrived.
 */
class HttpClient
{
  public:
    typedef std::vector<std::string> Headers;
    /** The status of a request that got no answer; the server may have done it. */
    static constexpr long kNoAnswer = -1;
    /** The status of a request that never left: its host could not be resolved or reached. */
    static constexpr long kNotSent = -2;
    /** The status of a request that was not sent because a local file could not be opened. */
    static constexpr long kLocalError = -3;
    /** curl's progress callback: totals and amounts done so far, down and up. */
    typedef int (*Progress)(void *data, double downTotal, double downNow, double upTotal,
                            double upNow);

    HttpClient();
    ~HttpClient();

    long Get(const std::string &url, const Headers &headers, std::string &body);
    long Post(const std::string &url, const std::string &data, const Headers &headers,
              std::string &body);
    long Put(const std::string &url, const std::string &data, const Headers &headers,
             std::string &body);
    /** Saves the response body to path; follows redirects. A failed download leaves no file. */
    long Download(const std::string &url, const Headers &headers, const std::string &path,
                  Progress progress);
    /** Sends the file at path as the body of a PUT, read as it goes. */
    long Upload(const std::string &url, const Headers &headers, const std::string &path,
                Progress progress, std::string &body);

    /** The Location header of the last response; "" when it had none. */
    std::string Location() const;
    /** text with URL-unsafe characters percent-encoded. */
    std::string Escape(const std::string &text);

    /** True for a 2xx status. */
    static bool Succeeded(long status);
    /**
     * The status of a request that curl could not complete.
     * @param result curl's error
     * @return kNotSent when the request never left, kNoAnswer otherwise
     */
    static long FailedStatus(CURLcode result);
    /**
     * Gives a fresh curl handle the settings every request shares: the URL, the user agent,
     * certificate checks against the shipped CA bundle, and the timeouts.
     */
    static void ApplyDefaults(CURL *curl, const std::string &url);
    /** curl write callback that appends the received data to the std::string at body. */
    static size_t AppendToString(char *data, size_t size, size_t count, void *body);

  private:
    HttpClient(const HttpClient &);
    HttpClient &operator=(const HttpClient &);

    /** Resets the handle to the settings every request shares. */
    void Prepare(const std::string &url);
    /** Runs the prepared request with headers; the status, or FailedStatus(). */
    long Perform(const Headers &headers);

    static size_t KeepHeader(char *data, size_t size, size_t count, void *client);

    CURL *m_curl;
    std::string m_location;
};
