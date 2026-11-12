// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClientCredentials.h"

#include "HttpClient.h"
#include "Json.h"
#include "log.h"

namespace
{
const char kPublishedUrl[] = "https://lmipsum.github.io/own-storage/clients.json";

// scripts/obfuscate-client.py reads this line; keep it on one line.
const char kObfuscationKey[] = "ba3b3c5e8ff3d58002d71e609e327025560933c50e9972ff4bddd266ccac600a";

int HexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

std::string AsIs(const std::string &text)
{
    return text;
}
} // namespace

bool ClientCredentials::IsComplete() const
{
    return !id.empty() && !secret.empty();
}

ClientCredentials ReadCredentials(const Json &file, const std::string &provider,
                                  std::string (*decode)(const std::string &))
{
    const Json client = file[provider];
    ClientCredentials credentials;
    credentials.id = decode(client["client_id"].Str());
    credentials.secret = decode(client["client_secret"].Str());
    return credentials;
}

ClientCredentials LoadUserCredentials(const std::string &path, const std::string &provider)
{
    return ReadCredentials(Json::Load(path), provider, &AsIs);
}

ClientCredentials FetchPublishedCredentials(HttpClient &http, const std::string &provider)
{
    std::string body;
    const long status = http.Get(kPublishedUrl, HttpClient::Headers(), body);
    if (!HttpClient::Succeeded(status))
    {
        debugPrintf("The published client could not be fetched: %ld\n", status);
        return ClientCredentials();
    }
    return ReadCredentials(Json::Parse(body), provider, &Deobfuscate);
}

std::string Deobfuscate(const std::string &hex)
{
    const std::string key = kObfuscationKey;
    if (hex.size() % 2 != 0)
    {
        return std::string();
    }
    std::string text;
    for (std::string::size_type i = 0; i < hex.size(); i += 2)
    {
        const int high = HexDigit(hex[i]);
        const int low = HexDigit(hex[i + 1]);
        if (high < 0 || low < 0)
        {
            return std::string();
        }
        text += static_cast<char>(((high << 4) | low) ^ key[(i / 2) % key.size()]);
    }
    return text;
}
