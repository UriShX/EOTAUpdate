#ifndef UPDATE_CHECKER
#define UPDATE_CHECKER

#include <Arduino.h>

/**
 * @brief Utility class to automatically fetch and install firmware updates on ESP32
 *
 * EOTAUpdate can periodically check for updates on a remote server, fetch and
 * flash new firmware.
 *
 * If the URL passed in the constructor begins with https, no non-secure connections will be made
 * to avoid misconfigurations or potential attack vectors.
 *
 * Setup:
 *  - Create a publicly accessible configuration file containing four lines (no spaces):
 *      https://myserver/ota/lastBuild.bin  <- Full URL of firmware to install
 *      3                                   <- Integer version of the new firmware
 *      a2fbf5835de079474e820ceda2ca9986    <- [optional] MD5 of the binary
 *      1.3                                 <- [optional] Version string, useful for logging
 *
 *    Let's assume it is publicly accessible at https://myserver/ota/cfg.txt
 *
 *  - Add these lines to your code:
 *      #include <EOTAUpdate.h>
 *      const unsigned VERSION_NUMBER = 1;
 *      const String   UPDATE_URL     = "https://myserver/ota/cfg.txt";
 *      EOTAUpdate updater(UPDATE_URL, VERSION_NUMBER);
 *    Then, in the loop() function add
 *      updater.CheckAndUpdate();
 * EOTAUpdate will search for a firmware update every minute by default.
 *
 * Limitations:
 *  - As of now, self-signed certificates will not work over SSL
 *
 */

typedef enum {
    eota_ok,
    eota_error,
    eota_failed,
    eota_no_match,
    eota_runaway,
    eota_size_error
} eota_reponses_t;

class EOTAUpdate
{
private:
    static const unsigned long UPDATE_INTERVAL_MS    = 60 * 60 * 1000;  // 1 hour

public:
    /**
     * @brief Create an EOTAUpdate instance
     * @param url                   URL pointing to the OTA configuration text file
     * @param currentVersion        Current running software version
     * @param updateIntervalMs      Min amount of time to between update checks in Ms
     */
    EOTAUpdate(
        const String &url,
        const unsigned currentVersion,
        const unsigned long updateIntervalMs    = UPDATE_INTERVAL_MS);

    /**
     * @brief Create an EOTAUpdate instance
     * @param url                   URL pointing to the OTA configuration text file
     * @param currentVersionStr     Current running software version
     * @param updateIntervalMs      Min amount of time to between update checks in Ms
     */
    EOTAUpdate(
        const String &url,
        const String &currentVersionStr,
        const unsigned long updateIntervalMs    = UPDATE_INTERVAL_MS);

    /**
     * @brief Check for an update and, if available, fetch it and flash it.
     *
     * @param force     By default update check frequency is limited by the constructor
     *                  param updateIntervalMs. If set to True, the last update time will
     *                  be ignored.
     * @return true     An update has been found and flashed. Technically this should never
     *                  be returned as the Microcontroller will be rebooted after a
     *                  successfull update.
     * @return false    Either no update was available or flashing failed
     */
    eota_reponses_t Check(bool force = false);
    eota_reponses_t CheckAndUpdate(bool force = false);
    void print_versions();

private:
    eota_reponses_t GetUpdateFWURL(String &_binURL, String &_binMD5);
    eota_reponses_t GetUpdateFWURL(String &_binURL, String &_binMD5, const String &url, const uint16_t retries = 5);
    eota_reponses_t PerformOTA(String &_binURL, String &_binMD5);
    bool parseSemVer(String _semVer, uint8_t (*_numArray)[4]);

private:
    String _url;
    const bool _forceSSL;
    const unsigned _currentVersion;
    const String _versionStr;
    uint8_t _currentVersionArr[4] = {0};
    const unsigned long _updateIntervalMs;
    unsigned long _lastUpdateMs;
    String _binURL;
    String _binMD5;
};

#endif // UPDATE_CHECKER