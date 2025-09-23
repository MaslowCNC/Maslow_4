// Copyright (c) 2024 - FluidNC Team
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"

#ifdef ENABLE_WIFI

#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include "../Logging.h"

namespace Maslow {
    class AutoUpdate {
    public:
        static bool isUpdateAvailable(const std::string& updateUrl, const std::string& currentVersion);
        static bool downloadAndInstallUpdate(const std::string& updateUrl, const std::string& newVersion);
        static void checkForUpdate();
        
    private:
        static std::string getCurrentVersion();
        static std::string getLatestVersionFromGitHub(const std::string& updateUrl);
        static bool compareVersions(const std::string& current, const std::string& latest);
        static bool downloadFile(const std::string& url, const std::string& filePath, size_t expectedSize = 0);
        static bool validateFile(const std::string& filePath, size_t expectedSize);
        static bool installFirmware(const std::string& firmwarePath);
        static bool installWebUI(const std::string& webUIPath);
        static std::string followRedirects(HTTPClient& http, const std::string& url, int maxRedirects = 5);
        
        static bool _updateInProgress;
        static uint32_t _lastUpdateCheck;
        static const uint32_t UPDATE_CHECK_INTERVAL = 30000; // 30 seconds after WiFi connection
    };
}

#endif // ENABLE_WIFI