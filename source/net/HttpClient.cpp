// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/HttpClient.h"

#include "app/Paths.h"
#include "app/DebugLog.h"

#include <cstdio>
#include <strings.h>

namespace
{
const char kUserAgent[] = "Own Storage";
const long kConnectTimeoutSeconds = 30;
// A transfer that moves less than this many bytes a second for this long has stalled, and fails
// instead of holding up the sync; Drive's own pauses, such as checking an upload, are shorter.
const long kStalledBytesPerSecond = 1;
const long kStalledSeconds = 60;

size_t WriteTo(char *data, size_t size, size_t count, void *file)
{
    return std::fwrite(data, size, count, static_cast<FILE *>(file)) * size;
}

size_t ReadFrom(char *data, size_t size, size_t count, void *file)
{
    return std::fread(data, size, count, static_cast<FILE *>(file)) * size;
}

/** The headers as a curl list; the caller frees it. */
curl_slist *BuildHeaderList(const HttpClient::Headers &headers)
{
    curl_slist *list = NULL;
    for (size_t i = 0; i < headers.size(); ++i)
    {
        list = curl_slist_append(list, headers[i].c_str());
    }
    return list;
}

void ReportProgress(CURL *curl, HttpClient::Progress progress)
{
    if (progress != NULL)
    {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress);
    }
}
} // namespace

HttpClient::HttpClient() : m_curl(curl_easy_init()) {}

HttpClient::~HttpClient()
{
    curl_easy_cleanup(m_curl);
}

void HttpClient::ApplyDefaults(CURL *curl, const std::string &url)
{
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_CAINFO, APP_DIR "cacert.pem");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, kStalledBytesPerSecond);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, kStalledSeconds);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
}

size_t HttpClient::AppendToString(char *data, size_t size, size_t count, void *body)
{
    static_cast<std::string *>(body)->append(data, size * count);
    return size * count;
}

void HttpClient::Prepare(const std::string &url)
{
    // A reset keeps the open connections, so the next request can reuse them.
    curl_easy_reset(m_curl);
    m_location.clear();
    ApplyDefaults(m_curl, url);
    curl_easy_setopt(m_curl, CURLOPT_HEADERFUNCTION, &HttpClient::KeepHeader);
    curl_easy_setopt(m_curl, CURLOPT_HEADERDATA, this);
}

long HttpClient::Perform(const Headers &headers)
{
    curl_slist *list = BuildHeaderList(headers);
    curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, list);
    char error[CURL_ERROR_SIZE] = "";
    curl_easy_setopt(m_curl, CURLOPT_ERRORBUFFER, error);

    const CURLcode result = curl_easy_perform(m_curl);
    curl_slist_free_all(list);
    if (result != CURLE_OK)
    {
        debugPrintf("HTTP request failed: %s\n", error[0] ? error : curl_easy_strerror(result));
        return -1;
    }
    long status = -1;
    curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &status);
    return status;
}

long HttpClient::Get(const std::string &url, const Headers &headers, std::string &body)
{
    Prepare(url);
    body.clear();
    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, &HttpClient::AppendToString);
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &body);
    return Perform(headers);
}

long HttpClient::Post(const std::string &url, const std::string &data, const Headers &headers,
                      std::string &body)
{
    Prepare(url);
    body.clear();
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(data.size()));
    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, &HttpClient::AppendToString);
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &body);
    return Perform(headers);
}

long HttpClient::Put(const std::string &url, const std::string &data, const Headers &headers,
                     std::string &body)
{
    Prepare(url);
    body.clear();
    curl_easy_setopt(m_curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(data.size()));
    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, &HttpClient::AppendToString);
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &body);
    return Perform(headers);
}

long HttpClient::Download(const std::string &url, const Headers &headers, const std::string &path,
                          Progress progress)
{
    FILE *file = std::fopen(path.c_str(), "wb");
    if (file == NULL)
    {
        debugPrintf("Cannot write %s\n", path.c_str());
        return -1;
    }
    Prepare(url);
    curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, &WriteTo);
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, file);
    ReportProgress(m_curl, progress);
    const long status = Perform(headers);
    std::fclose(file);
    // The file holds an error page or a part of the content, which must not pass for the file.
    if (!Succeeded(status))
    {
        std::remove(path.c_str());
    }
    return status;
}

long HttpClient::Upload(const std::string &url, const Headers &headers, const std::string &path,
                        Progress progress, std::string &body)
{
    FILE *file = std::fopen(path.c_str(), "rb");
    if (file == NULL)
    {
        debugPrintf("Cannot read %s\n", path.c_str());
        return -1;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    Prepare(url);
    body.clear();
    curl_easy_setopt(m_curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(m_curl, CURLOPT_INFILESIZE, size);
    curl_easy_setopt(m_curl, CURLOPT_READFUNCTION, &ReadFrom);
    curl_easy_setopt(m_curl, CURLOPT_READDATA, file);
    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, &HttpClient::AppendToString);
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &body);
    ReportProgress(m_curl, progress);
    const long status = Perform(headers);
    std::fclose(file);
    return status;
}

size_t HttpClient::KeepHeader(char *data, size_t size, size_t count, void *client)
{
    const size_t length = size * count;
    static const char name[] = "Location:";
    const size_t nameLength = sizeof name - 1;
    if (length > nameLength && strncasecmp(data, name, nameLength) == 0)
    {
        std::string value(data + nameLength, length - nameLength);
        const size_t first = value.find_first_not_of(" \t");
        const size_t last = value.find_last_not_of(" \t\r\n");
        static_cast<HttpClient *>(client)->m_location =
            first == std::string::npos ? std::string() : value.substr(first, last - first + 1);
    }
    return length;
}

std::string HttpClient::Location() const
{
    return m_location;
}

std::string HttpClient::Escape(const std::string &text)
{
    char *escaped = curl_easy_escape(m_curl, text.c_str(), static_cast<int>(text.size()));
    const std::string result = escaped == NULL ? std::string() : std::string(escaped);
    curl_free(escaped);
    return result;
}

bool HttpClient::Succeeded(long status)
{
    return status >= 200 && status < 300;
}
