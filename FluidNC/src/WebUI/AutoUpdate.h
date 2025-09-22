// Copyright (c) 2024 - FluidNC Contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"

#ifdef ENABLE_WIFI

namespace WebUI {
    class AutoUpdate {
    public:
        static bool checkForUpdate();
        static bool downloadAndInstallUpdate(const std::string& firmwareUrl, const std::string& webUIUrl);

    private:
        static std::string getLatestReleaseInfo();
        static bool        downloadFile(const std::string& url, const std::string& filename);
        static bool        downloadFileToLocalFS(const std::string& url, const std::string& filename);
        static bool        downloadAndInstallFirmware(const std::string& firmwareUrl);
        static bool        installWebUI(const std::string& filename);
    };
}

#endif