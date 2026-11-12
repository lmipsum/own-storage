// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

class HttpClient;
class Json;

/** The OAuth client an app signs in as with a cloud provider. */
struct ClientCredentials
{
    std::string id;
    std::string secret;

    bool IsComplete() const;
};

/**
 * The user's own client for provider from the plain JSON file at path:
 * {"google": {"client_id": "...", "client_secret": "..."}}. Incomplete when there is none.
 */
ClientCredentials LoadUserCredentials(const std::string &path, const std::string &provider);

/**
 * The app's client for provider from the file published with the project, in the same layout
 * with obfuscated values. Incomplete when it cannot be fetched.
 */
ClientCredentials FetchPublishedCredentials(HttpClient &http, const std::string &provider);

/** The client for provider in a file's content; values pass through decode. */
ClientCredentials ReadCredentials(const Json &file, const std::string &provider,
                                  std::string (*decode)(const std::string &));

/**
 * Undoes scripts/obfuscate-client.py: hex text XORed with a key kept in this program. This is
 * no protection, as the key ships with the app; it keeps the published file from matching the
 * patterns that services scan for leaked credentials. "" for text that is not valid hex.
 */
std::string Deobfuscate(const std::string &hex);
