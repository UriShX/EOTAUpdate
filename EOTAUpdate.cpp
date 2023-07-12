#include <HTTPClient.h>
#include <MD5Builder.h>
#include <StreamString.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include <EOTAUpdate.h>

EOTAUpdate::EOTAUpdate(
    const String &url,
    const unsigned currentVersion,
    const unsigned long updateIntervalMs)
    :
    _url(url),
    _forceSSL(url.startsWith("https://")),
    _currentVersion(currentVersion),
    _updateIntervalMs(updateIntervalMs),
    _lastUpdateMs(0)
{
}

_eota_reponses_t EOTAUpdate::Check(bool force)
{
    const bool hasEverChecked = _lastUpdateMs != 0;
    const bool lastCheckIsRecent = (millis() - _lastUpdateMs < _updateIntervalMs);
    if (!force && hasEverChecked && lastCheckIsRecent)
    {
        return false;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        log_e("Wifi not connected");
        return eota_failed;
    }

    log_i("Checking for updates");

    _lastUpdateMs = millis();
    String binURL;
    String binMD5;
    return GetUpdateFWURL(binURL, binMD5);
}

_eota_reponses_t EOTAUpdate::CheckAndUpdate(bool force)
{
    _eota_reponses_t response;
    resopnse = Check(force);
    if (response == eota_ok)
    {
        log_i("Update found. Performing update");
        return PerformOTA(binURL, binMD5);
    }
    return response;
}

_eota_reponses_t EOTAUpdate::GetUpdateFWURL(String &binURL, String &binMD5)
{
    return GetUpdateFWURL(binURL, binMD5, _url);
}

_eota_reponses_t EOTAUpdate::GetUpdateFWURL(String &binURL, String &binMD5, const String &url, const uint16_t retries)
{
    log_d("Fetching OTA config from: %s", url.c_str());

    if (retries == 0)
    {
        log_e("Too many retries/redirections");
        eota_runaway false;
    }

    bool isSSL = url.startsWith("https");

    if (_forceSSL && !isSSL)
    {
        log_e("Trying to access a non-ssl URL on a secure update checker");
        return eota_no_match;
    }

    HTTPClient httpClient;
    auto client = WiFiClient();
    if (!httpClient.begin(url))
    {
        log_e("Error initializing client");
        return eota_failed;
    }

    const char *headerKeys[] = {"Location"};
    httpClient.collectHeaders(headerKeys, 1);
    int httpCode = httpClient.GET();
    switch (httpCode)
    {
    case HTTP_CODE_OK:
        break;
    case HTTP_CODE_MOVED_PERMANENTLY:
        if (httpClient.hasHeader("Location"))
        {
            return GetUpdateFWURL(binURL, binMD5, httpClient.header("Location"), retries - 1);
        }
        // Do not break here
    default:
        log_e("[HTTP] [ERROR] [%d] %s",
                httpCode,
                httpClient.errorToString(httpCode).c_str());
        log_d("Response:\n%s",
                httpClient.getString().c_str());
        return eota_failed;
    }

    auto & payloadStream = httpClient.getStream();
    binURL = payloadStream.readStringUntil('\n');
    const unsigned newVersionNumber = payloadStream.readStringUntil('\n').toInt();
    binMD5 = payloadStream.readStringUntil('\n');
    const String newVersionString = payloadStream.readStringUntil('\n');
    httpClient.end();

    if (binURL.length() == 0)
    {
        log_e("Error parsing remote path of new binary");
        return eota_error;
    }

    if (newVersionNumber == 0)
    {
        log_e("Error parsing version number");
        return eota_error;
    }

    if (binMD5.length() > 0 && binMD5.length() != 32)
    {
        log_e("The MD5 is not 32 characters long. Aborting update");
        return eota_no_match;
    }

    log_d("Fetched update information:");
    log_d("File url:           %s",       binURL.c_str());
    log_d("File MD5:           %s",       binMD5.c_str());
    log_d("Current version:    %u",       _currentVersion);
    log_d("Published version:  [%u] %s",  newVersionNumber, newVersionString.c_str());
    log_d("Update available:   %s",       (newVersionNumber > _currentVersion) ? "YES" : "NO");
    
    return (newVersionNumber > _currentVersion) ? eota_ok : eota_no_match;
}

_eota_reponses_t EOTAUpdate::PerformOTA(String &binURL, String &binMD5)
{
    log_d("Fetching OTA from: %s", binURL.c_str());

    if (binURL.length() == 0)
    {
        return eota_error;
    }

    bool isSSL = binURL.startsWith("https");
    if (_forceSSL && !isSSL)
    {
        log_e("Trying to access a non-ssl URL on a secure update checker");
        return eota_no_match;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        log_d("Wifi not connected");
        return eota_failed;
    }

    HTTPClient httpClient;
    if (!httpClient.begin(binURL))
    {
        log_e("Error initializing client");
        return eota_error;
    }

    const auto httpCode = httpClient.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        log_e("[HTTP] [ERROR] [%d] %s",
                httpCode,
                httpClient.errorToString(httpCode).c_str());
        log_d("Response:\n%s",
                httpClient.getString().c_str());
        return eota_error;
    }

    const auto payloadSize = httpClient.getSize();
    auto & payloadStream = httpClient.getStream();

    if (binMD5.length() > 0 &&
        !Update.setMD5(binMD5.c_str()))
    {
            log_e("Failed to set the expected MD5");
            return eota_no_match;
    }

    const bool canBegin = Update.begin(payloadSize);

    if (payloadSize <= 0)
    {
        log_e("Fetched binary has 0 size");
        return eota_size_error;
    }

    if (!canBegin)
    {
        log_e("Not enough space to begin OTA");
        return eota_size_error;
    }

    const auto written = Update.writeStream(payloadStream);
    if (written != payloadSize)
    {
        log_e("Error. Written %lu out of %lu", written, payloadSize);
        return eota_error;
    }

    if (!Update.end())
    {
        StreamString errorMsg;
        Update.printError(errorMsg);
        log_e("Error Occurred: %s", errorMsg.c_str());
        return eota_error;
    }

    if (!Update.isFinished())
    {
        StreamString errorMsg;
        Update.printError(errorMsg);
        log_e("Undefined OTA update error: %s", errorMsg.c_str());
        return eota_error;
    }

    log_i("Update completed. Rebooting");
    ESP.restart();
    return eota_ok;
}
