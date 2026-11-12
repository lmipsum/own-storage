// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

class HttpClient;

/**
 * OAuth 2.0 sign-in for devices without a browser (RFC 8628) against Google: the console
 * shows a code, the user approves it on another device, and the app keeps the refresh token.
 */
class DeviceAuth
{
  public:
    enum Status
    {
        Approved,
        Pending,
        Failed
    };

    DeviceAuth(HttpClient &http, const std::string &clientId, const std::string &clientSecret);

    /** Asks for a new code; the code to show the user, or "" when the request failed. */
    std::string Start();
    /** The page where the user enters the code from Start(). */
    const std::string &VerificationUrl() const;
    /** Asks whether the user has approved the code yet; on approval the tokens are kept. */
    Status Poll();
    /** Gets a new access token with the refresh token; false when it is no longer valid. */
    bool Refresh();

    const std::string &AccessToken() const;
    const std::string &RefreshToken() const;
    void SetRefreshToken(const std::string &token);

  private:
    HttpClient &m_http;
    const std::string m_clientId;
    const std::string m_clientSecret;
    std::string m_deviceCode;
    std::string m_verificationUrl;
    std::string m_accessToken;
    std::string m_refreshToken;
};
