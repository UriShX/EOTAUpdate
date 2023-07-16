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

EOTAUpdate::EOTAUpdate(
    const String &url,
    const String &currentVersionStr,
    const unsigned long updateIntervalMs)
    :
    _url(url),
    _forceSSL(url.startsWith("https://")),
    _currentVersion(0),
    _versionStr(currentVersionStr),
    _updateIntervalMs(updateIntervalMs),
    _lastUpdateMs(0)
{
    parseSemVer(currentVersionStr, &_currentVersionArr);
}


void EOTAUpdate::print_versions()
{
    if (_currentVersionArr[0] != 0 && _currentVersionArr[1] != 0 && _currentVersionArr[2] != 0 && _currentVersionArr[3] != 0)
        Serial.printf("version passed to OTA lib: %u\r\n", _currentVersion);
    else
        Serial.printf("version passed to OTA lib - parsed: %u.%u.%u%c, string recieved: %s\r\n", _currentVersionArr[0], _currentVersionArr[1], _currentVersionArr[2], _currentVersionArr[3], _versionStr);

}

eota_reponses_t EOTAUpdate::Check(bool force)
{
    const bool hasEverChecked = _lastUpdateMs != 0;
    const bool lastCheckIsRecent = (millis() - _lastUpdateMs < _updateIntervalMs);
    if (!force && hasEverChecked && lastCheckIsRecent)
    {
        return eota_no_match;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        log_e("Wifi not connected");
        return eota_failed;
    }

    log_i("Checking for updates");

    _lastUpdateMs = millis();
    return GetUpdateFWURL(_binURL, _binMD5);
}

eota_reponses_t EOTAUpdate::CheckAndUpdate(bool force)
{
    eota_reponses_t response;
    response = Check(force);
    if (response == eota_ok)
    {
        log_i("Update found. Performing update");
        return PerformOTA(_binURL, _binMD5);
    }
    return response;
}

eota_reponses_t EOTAUpdate::GetUpdateFWURL(String &_binURL, String &_binMD5)
{
    return GetUpdateFWURL(_binURL, _binMD5, _url);
}

eota_reponses_t EOTAUpdate::GetUpdateFWURL(String &_binURL, String &_binMD5, const String &url, const uint16_t retries)
{
    log_d("Fetching OTA config from: %s", url.c_str());

    if (retries == 0)
    {
        log_e("Too many retries/redirections");
        return eota_runaway;
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
            return GetUpdateFWURL(_binURL, _binMD5, httpClient.header("Location"), retries - 1);
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

    unsigned _newVersionNumber = 0;
    uint8_t newVersionArr[4] = {0};

    auto & payloadStream = httpClient.getStream();
    _binURL = payloadStream.readStringUntil('\n');
    const String newVersionNumber = payloadStream.readStringUntil('\n');
    if (_currentVersionArr[0] == 0 && _currentVersionArr[1] == 0 && _currentVersionArr[2] == 0 && _currentVersionArr[3] == 0)
        _newVersionNumber = newVersionNumber.toInt();
    else
        parseSemVer(newVersionNumber, &newVersionArr);
    _binMD5 = payloadStream.readStringUntil('\n');
    const String newVersionString = payloadStream.readStringUntil('\n');
    httpClient.end();

    if (_binURL.length() == 0)
    {
        log_e("Error parsing remote path of new binary");
        return eota_error;
    }

    if (newVersionNumber == 0)
    {
        log_e("Error parsing version number");
        return eota_error;
    }

    if (_binMD5.length() > 0 && _binMD5.length() != 32)
    {
        log_e("The MD5 is not 32 characters long. Aborting update");
        return eota_no_match;
    }

    bool updateAvailable = false;
    if (_currentVersionArr[0] != 0 && _currentVersionArr[1] != 0 && _currentVersionArr[2] != 0 && _currentVersionArr[3] != 0)
        updateAvailable = (_newVersionNumber > _currentVersion) ? true : false;
    else
        for (uint8_t i = 0; i < sizeof(_currentVersionArr); i++)
            updateAvailable = (newVersionArr[i] > _currentVersionArr[i]) ? true : false; 
        
    log_d("Fetched update information:");
    log_d("File url:           %s",       _binURL.c_str());
    log_d("File MD5:           %s",       _binMD5.c_str());
    if (_currentVersionArr[0] != 0 && _currentVersionArr[1] != 0 && _currentVersionArr[2] != 0 && _currentVersionArr[3] != 0)
        log_d("Current version:    %u",       _currentVersion);
    else
        log_d("Current version: %s", _versionStr);
    log_d("Update available:   %s",       (updateAvailable) ? "YES" : "NO");
    log_d("Published version:  [%u] %s",  newVersionNumber, newVersionString.c_str());

    
    return (updateAvailable) ? eota_ok : eota_no_match;
}

eota_reponses_t EOTAUpdate::PerformOTA(String &_binURL, String &_binMD5)
{
    log_d("Fetching OTA from: %s", _binURL.c_str());

    if (_binURL.length() == 0)
    {
        return eota_error;
    }

    bool isSSL = _binURL.startsWith("https");
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
    if (!httpClient.begin(_binURL))
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

    if (_binMD5.length() > 0 &&
        !Update.setMD5(_binMD5.c_str()))
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

bool EOTAUpdate::parseSemVer(String _semVer, uint8_t (*_numArray)[4])
{
  _semVer.toLowerCase();
  uint8_t dot = 0;
  uint8_t last_index = 0;
  for (uint8_t i = 0; i < 3; i++)
  {
    dot = _semVer.indexOf('.', last_index);
    String tmp = _semVer.substring(last_index, dot);
    last_index = dot+1;
    (*_numArray)[i] = tmp.toInt();
  }
  (*_numArray)[3] = _semVer.charAt(_semVer.length()-1);
  if (!isalpha((*_numArray)[3]))
    (*_numArray)[3] = 0;
  String _comp;
  for (uint8_t j = 0; j < 3; j++)
  {
    String tmp = String((*_numArray)[j]);
    _comp.concat(tmp);
    _comp.concat('.');
  }
  _comp.setCharAt(_comp.length() - 1, (*_numArray)[3]);
  if (_comp.compareTo(_semVer) != 0)
    return false;
  log_i("parsed semantic version: %u.%u.%u%c, received semantic version: %s\r\n",
                                (*_numArray)[0], (*_numArray)[1], (*_numArray)[2], (*_numArray)[3], _semVer);
  return true;
}
