// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeviceAuth.h"

#include "HttpClient.h"
#include "Json.h"
#include "log.h"

namespace
{
const char kCodeUrl[] = "https://oauth2.googleapis.com/device/code";
const char kTokenUrl[] = "https://oauth2.googleapis.com/token";
// Only the files the app creates itself.
const char kScope[] = "https://www.googleapis.com/auth/drive.file";
const char kFormHeader[] = "Content-Type: application/x-www-form-urlencoded";
} // namespace

DeviceAuth::DeviceAuth(HttpClient &http) : m_http(http) {}

void DeviceAuth::SetClient(const ClientCredentials &client)
{
    m_client = client;
}

std::string DeviceAuth::Start()
{
    const std::string form =
        "client_id=" + m_http.Escape(m_client.id) + "&scope=" + m_http.Escape(kScope);
    std::string body;
    const long status = m_http.Post(kCodeUrl, form, HttpClient::Headers(1, kFormHeader), body);
    const Json reply = Json::Parse(body);
    if (!HttpClient::Succeeded(status) || !reply.Has("device_code"))
    {
        debugPrintf("Device code request failed: %ld %s\n", status, reply["error"].Str().c_str());
        return std::string();
    }
    m_deviceCode = reply["device_code"].Str();
    m_verificationUrl = reply["verification_url"].Str();
    return reply["user_code"].Str();
}

const std::string &DeviceAuth::VerificationUrl() const
{
    return m_verificationUrl;
}

DeviceAuth::Status DeviceAuth::Poll()
{
    const std::string form = "client_id=" + m_http.Escape(m_client.id) +
                             "&client_secret=" + m_http.Escape(m_client.secret) +
                             "&device_code=" + m_http.Escape(m_deviceCode) +
                             "&grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code";
    std::string body;
    const long status = m_http.Post(kTokenUrl, form, HttpClient::Headers(1, kFormHeader), body);
    const Json reply = Json::Parse(body);
    if (HttpClient::Succeeded(status) && reply.Has("access_token"))
    {
        m_accessToken = reply["access_token"].Str();
        m_refreshToken = reply["refresh_token"].Str();
        return Approved;
    }
    const std::string error = reply["error"].Str();
    // RFC 8628 3.5: these two only mean the user has not answered yet.
    if (error == "authorization_pending" || error == "slow_down")
    {
        return Pending;
    }
    debugPrintf("Device sign-in failed: %ld %s\n", status, error.c_str());
    return Failed;
}

bool DeviceAuth::Refresh()
{
    const std::string form = "client_id=" + m_http.Escape(m_client.id) +
                             "&client_secret=" + m_http.Escape(m_client.secret) +
                             "&refresh_token=" + m_http.Escape(m_refreshToken) +
                             "&grant_type=refresh_token";
    std::string body;
    const long status = m_http.Post(kTokenUrl, form, HttpClient::Headers(1, kFormHeader), body);
    const Json reply = Json::Parse(body);
    if (!HttpClient::Succeeded(status) || !reply.Has("access_token"))
    {
        debugPrintf("Token refresh failed: %ld %s\n", status, reply["error"].Str().c_str());
        return false;
    }
    m_accessToken = reply["access_token"].Str();
    return true;
}

const std::string &DeviceAuth::AccessToken() const
{
    return m_accessToken;
}

const std::string &DeviceAuth::RefreshToken() const
{
    return m_refreshToken;
}

void DeviceAuth::SetRefreshToken(const std::string &token)
{
    m_refreshToken = token;
}
