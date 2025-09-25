// Copyright (c) 2024 - FluidNC Team  
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "AutoUpdate.h"

#ifdef ENABLE_WIFI

#include "../Machine/MachineConfig.h"
#include "../System.h"
#include "../FluidPath.h"
#include "Driver/localfs.h"
#include <FS.h>
#include <SD.h>
#include <ArduinoJson.h>

extern const char* git_info;  // From version.cpp

namespace Maslow {
    bool AutoUpdate::_updateInProgress = false;
    uint32_t AutoUpdate::_lastUpdateCheck = 0;
    bool AutoUpdate::_updateCheckCompleted = false;

    std::string AutoUpdate::getCurrentVersion() {
        // Extract version from git_info (format: "v3.7.x (Maslow-Main-abc123)")
        std::string version = git_info;
        log_debug("Autoupdate: Current version string: " << version);
        
        size_t space_pos = version.find(' ');
        if (space_pos != std::string::npos) {
            version = version.substr(0, space_pos);
        }
        
        log_debug("Autoupdate: Parsed current version: " << version);
        return version;
    }

    bool AutoUpdate::compareVersions(const std::string& current, const std::string& latest) {
        log_debug("Autoupdate: Comparing versions - current: '" << current << "', latest: '" << latest << "'");
        
        // Simple string comparison for now - this could be enhanced with proper semantic versioning
        // Remove 'v' prefix if present
        std::string currentClean = current;
        std::string latestClean = latest;
        
        if (currentClean.size() > 0 && currentClean[0] == 'v') {
            currentClean = currentClean.substr(1);
        }
        if (latestClean.size() > 0 && latestClean[0] == 'v') {
            latestClean = latestClean.substr(1);
        }
        
        bool isNewer = latestClean != currentClean;
        log_debug("Autoupdate: Version comparison result - newer version available: " << (isNewer ? "true" : "false"));
        return isNewer;
    }

    std::string AutoUpdate::followRedirects(HTTPClient& http, const std::string& url, int maxRedirects) {
        log_debug("Autoupdate: Following redirects for URL: " << url << ", max redirects: " << maxRedirects);
        
        std::string currentUrl = url;
        
        for (int i = 0; i < maxRedirects; i++) {
            http.begin(currentUrl.c_str());
            http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
            
            int httpCode = http.GET();
            log_debug("Autoupdate: HTTP response code: " << httpCode << " for URL: " << currentUrl);
            
            if (httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND || httpCode == HTTP_CODE_TEMPORARY_REDIRECT) {
                String location = http.getLocation();
                http.end();
                
                if (location.length() > 0) {
                    currentUrl = location.c_str();
                    log_debug("Autoupdate: Redirected to: " << currentUrl);
                } else {
                    log_error("Autoupdate: Redirect response but no Location header");
                    return "";
                }
            } else if (httpCode == HTTP_CODE_OK) {
                log_debug("Autoupdate: Final URL after redirects: " << currentUrl);
                http.end();
                return currentUrl;
            } else {
                log_error("Autoupdate: HTTP error: " << httpCode);
                http.end();
                return "";
            }
        }
        
        log_error("Autoupdate: Too many redirects");
        return "";
    }

    std::string AutoUpdate::getLatestVersionFromGitHub(const std::string& updateUrl) {
        log_info("Autoupdate: Checking for latest version from: " << updateUrl);
        
        HTTPClient http;
        WiFiClientSecure client;
        client.setInsecure(); // For simplicity - in production might want to verify certificates
        
        // Convert GitHub releases page URL to API URL
        std::string apiUrl = updateUrl;
        size_t githubPos = apiUrl.find("github.com/");
        if (githubPos != std::string::npos) {
            size_t releasesPos = apiUrl.find("/releases");
            if (releasesPos != std::string::npos) {
                std::string repoPath = apiUrl.substr(githubPos + 11, releasesPos - (githubPos + 11));
                apiUrl = "https://api.github.com/repos/" + repoPath + "/releases/latest";
            }
        }
        
        log_debug("Autoupdate: API URL: " << apiUrl);
        
        http.begin(client, apiUrl.c_str());
        http.addHeader("User-Agent", "FluidNC-AutoUpdate/1.0");
        http.setTimeout(10000); // 10 second timeout
        
        int httpCode = http.GET();
        log_debug("Autoupdate: GitHub API response code: " << httpCode);
        
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            log_debug("Autoupdate: GitHub API response length: " << payload.length());
            
            // Parse JSON response
            DynamicJsonDocument doc(8192);
            DeserializationError error = deserializeJson(doc, payload);
            
            if (error) {
                log_error("Autoupdate: Failed to parse GitHub API response: " << error.c_str());
                http.end();
                return "";
            }
            
            if (doc.containsKey("tag_name")) {
                std::string latestVersion = doc["tag_name"].as<std::string>();
                log_info("Autoupdate: Latest version found: " << latestVersion);
                http.end();
                return latestVersion;
            } else {
                log_error("Autoupdate: GitHub API response missing tag_name field");
            }
        } else {
            log_error("Autoupdate: Failed to fetch latest version, HTTP code: " << httpCode);
        }
        
        http.end();
        return "";
    }

    bool AutoUpdate::downloadFile(const std::string& url, const std::string& filePath, size_t expectedSize) {
        log_info("Autoupdate: Downloading file from: " << url << " to: " << filePath);
        
        HTTPClient http;
        WiFiClientSecure client;
        client.setInsecure();
        
        // Follow redirects to get the actual download URL
        std::string finalUrl = followRedirects(http, url, 5);
        if (finalUrl.empty()) {
            log_error("Autoupdate: Failed to resolve download URL, starting URL was: " << url);
            return false;
        }
        
        if (finalUrl != url) {
            log_debug("Autoupdate: Following redirects from " << url << " to " << finalUrl);
        }
        
        log_debug("Autoupdate: Starting download from: " << finalUrl);
        
        http.begin(client, finalUrl.c_str());
        http.addHeader("User-Agent", "FluidNC-AutoUpdate/1.0");
        http.setTimeout(60000); // 60 second timeout for downloads
        
        int httpCode = http.GET();
        log_debug("Autoupdate: Download HTTP response code: " << httpCode);
        
        if (httpCode == HTTP_CODE_OK) {
            int contentLength = http.getSize();
            log_info("Autoupdate: Content length: " << contentLength << " bytes");
            
            if (expectedSize > 0 && expectedSize != contentLength) {
                log_warn("Autoupdate: Expected size (" << expectedSize << ") differs from actual size (" << contentLength << ")");
            }
            
            // Determine the appropriate file system
            FILE* file = nullptr;
            if (filePath.find("/sdcard/") == 0) {
                // SD card file
                if (!SD.exists("/")) {
                    log_error("Autoupdate: SD card not available for file: " << filePath);
                    http.end();
                    return false;
                }
                log_debug("Autoupdate: Writing to SD card: " << filePath);
                file = fopen(filePath.c_str(), "wb");
            } else {
                // Local filesystem file
                log_debug("Autoupdate: Writing to local filesystem: " << filePath);
                file = fopen(filePath.c_str(), "wb");
            }
            
            if (!file) {
                log_error("Autoupdate: Failed to create file: " << filePath << " (errno: " << errno << ")");
                http.end();
                return false;
            }
            
            WiFiClient* stream = http.getStreamPtr();
            size_t bytesDownloaded = 0;
            uint8_t buffer[1024];
            uint32_t lastProgressLog = millis();
            
            log_info("Autoupdate: Starting file transfer");
            
            while (http.connected() && bytesDownloaded < contentLength) {
                int bytesRead = stream->readBytes(buffer, sizeof(buffer));
                if (bytesRead > 0) {
                    size_t bytesWritten = fwrite(buffer, 1, bytesRead, file);
                    if (bytesWritten != bytesRead) {
                        log_error("Autoupdate: Write error, expected " << bytesRead << " bytes, wrote " << bytesWritten);
                        fclose(file);
                        http.end();
                        return false;
                    }
                    bytesDownloaded += bytesRead;
                    
                    // Log progress every 10KB or every 5 seconds
                    uint32_t now = millis();
                    if (bytesDownloaded % 10240 == 0 || (now - lastProgressLog) > 5000) {
                        int percent = (bytesDownloaded * 100) / contentLength;
                        log_info("Autoupdate: Downloaded " << bytesDownloaded << " / " << contentLength << " bytes (" << percent << "%)");
                        lastProgressLog = now;
                    }
                } else if (bytesRead == 0) {
                    log_debug("Autoupdate: No bytes read, stream may be finished or blocked");
                    delay(10); // Small delay to prevent tight loop
                } else {
                    log_error("Autoupdate: Read error: " << bytesRead);
                    break;
                }
                delay(1); // Yield to other tasks
            }
            
            fclose(file);
            http.end();
            
            log_info("Autoupdate: Download completed. Total bytes: " << bytesDownloaded << " / " << contentLength);
            
            if (bytesDownloaded != contentLength) {
                log_error("Autoupdate: Download incomplete. Expected: " << contentLength << ", got: " << bytesDownloaded);
                return false;
            }
            
            if (expectedSize > 0 && bytesDownloaded != expectedSize) {
                log_error("Autoupdate: Download size mismatch. Expected: " << expectedSize << ", got: " << bytesDownloaded);
                return false;
            }
            
            log_info("Autoupdate: File download successful: " << filePath);
            return true;
        } else {
            log_error("Autoupdate: Download failed with HTTP code: " << httpCode << " for URL: " << finalUrl);
            http.end();
            return false;
        }
    }

    bool AutoUpdate::validateFile(const std::string& filePath, size_t expectedSize) {
        log_debug("Autoupdate: Validating file: " << filePath);
        
        FILE* file = fopen(filePath.c_str(), "rb");
        if (!file) {
            log_error("Autoupdate: Cannot open file for validation: " << filePath);
            return false;
        }
        
        fseek(file, 0, SEEK_END);
        size_t fileSize = ftell(file);
        fclose(file);
        
        log_debug("Autoupdate: File size: " << fileSize << ", expected: " << expectedSize);
        
        if (expectedSize > 0 && fileSize != expectedSize) {
            log_error("Autoupdate: File size validation failed");
            return false;
        }
        
        log_debug("Autoupdate: File validation passed");
        return true;
    }

    bool AutoUpdate::installWebUI(const std::string& webUIPath) {
        log_info("Autoupdate: Installing web UI from: " << webUIPath);
        
        // Remove old index.html.gz if it exists
        if (remove("/index.html.gz") == 0) {
            log_debug("Autoupdate: Removed old index.html.gz");
        }
        
        // Copy downloaded file to final location
        FILE* sourceFile = fopen(webUIPath.c_str(), "rb");
        if (!sourceFile) {
            log_error("Autoupdate: Cannot open downloaded web UI file");
            return false;
        }
        
        FILE* destFile = fopen("/index.html.gz", "wb");
        if (!destFile) {
            log_error("Autoupdate: Cannot create final web UI file");
            fclose(sourceFile);
            return false;
        }
        
        // Copy file contents
        uint8_t buffer[1024];
        size_t bytesRead;
        while ((bytesRead = fread(buffer, 1, sizeof(buffer), sourceFile)) > 0) {
            fwrite(buffer, 1, bytesRead, destFile);
        }
        
        fclose(sourceFile);
        fclose(destFile);
        
        // Remove temporary file
        remove(webUIPath.c_str());
        
        log_info("Autoupdate: Web UI installation completed");
        return true;
    }

    bool AutoUpdate::installFirmware(const std::string& firmwarePath) {
        log_info("Autoupdate: Installing firmware from: " << firmwarePath);
        
        File firmwareFile = SD.open(firmwarePath.c_str(), FILE_READ);
        if (!firmwareFile) {
            log_error("Autoupdate: Cannot open firmware file");
            return false;
        }
        
        size_t firmwareSize = firmwareFile.size();
        log_debug("Autoupdate: Firmware size: " << firmwareSize);
        
        if (!Update.begin(firmwareSize)) {
            log_error("Autoupdate: Cannot start firmware update. Error: " << Update.errorString());
            firmwareFile.close();
            return false;
        }
        
        Update.onProgress([](size_t progress, size_t total) {
            if (progress % 65536 == 0) { // Log every 64KB
                log_debug("Autoupdate: Firmware update progress: " << (progress * 100 / total) << "%");
            }
        });
        
        size_t written = Update.writeStream(firmwareFile);
        firmwareFile.close();
        
        if (written != firmwareSize) {
            log_error("Autoupdate: Firmware write failed. Written: " << written << ", expected: " << firmwareSize);
            Update.abort();
            return false;
        }
        
        if (!Update.end()) {
            log_error("Autoupdate: Firmware update end failed. Error: " << Update.errorString());
            return false;
        }
        
        log_info("Autoupdate: Firmware update completed successfully");
        return true;
    }

    bool AutoUpdate::downloadAndInstallUpdate(const std::string& updateUrl, const std::string& newVersion) {
        log_info("Autoupdate: Starting download and installation for version: " << newVersion);
        log_debug("Autoupdate: Free heap at start: " << ESP.getFreeHeap() << " bytes");
        
        if (_updateInProgress) {
            log_warn("Autoupdate: Update already in progress");
            return false;
        }
        
        _updateInProgress = true;
        log_debug("Autoupdate: Set update in progress flag");
        
        // Get release information from GitHub API
        HTTPClient http;
        WiFiClientSecure client;
        client.setInsecure();
        
        std::string apiUrl = updateUrl;
        size_t githubPos = apiUrl.find("github.com/");
        if (githubPos != std::string::npos) {
            size_t releasesPos = apiUrl.find("/releases");
            if (releasesPos != std::string::npos) {
                std::string repoPath = apiUrl.substr(githubPos + 11, releasesPos - (githubPos + 11));
                apiUrl = "https://api.github.com/repos/" + repoPath + "/releases/tags/" + newVersion;
                log_debug("Autoupdate: Converted to API URL: " << apiUrl);
            } else {
                log_error("Autoupdate: Invalid GitHub URL format - missing /releases");
                _updateInProgress = false;
                return false;
            }
        } else {
            log_error("Autoupdate: Not a GitHub URL: " << updateUrl);
            _updateInProgress = false;
            return false;
        }
        
        log_info("Autoupdate: Fetching release information from GitHub API");
        
        http.begin(client, apiUrl.c_str());
        http.addHeader("User-Agent", "FluidNC-AutoUpdate/1.0");
        http.setTimeout(15000); // 15 second timeout
        
        int httpCode = http.GET();
        log_debug("Autoupdate: GitHub API HTTP response code: " << httpCode);
        
        if (httpCode != HTTP_CODE_OK) {
            log_error("Autoupdate: Failed to get release information, HTTP code: " << httpCode << ", URL: " << apiUrl);
            _updateInProgress = false;
            http.end();
            return false;
        }
        
        String payload = http.getString();
        int payloadLength = payload.length();
        http.end();
        
        log_debug("Autoupdate: Received " << payloadLength << " bytes from GitHub API");
        log_debug("Autoupdate: Free heap after API call: " << ESP.getFreeHeap() << " bytes");
        
        DynamicJsonDocument doc(16384);
        DeserializationError error = deserializeJson(doc, payload);
        
        if (error) {
            log_error("Autoupdate: Failed to parse release information: " << error.c_str());
            log_debug("Autoupdate: JSON payload preview: " << payload.substring(0, 200).c_str());
            _updateInProgress = false;
            return false;
        }
        
        log_info("Autoupdate: Successfully parsed release information");
        
        JsonArray assets = doc["assets"];
        std::string indexGzUrl = "";
        std::string firmwareBinUrl = "";
        
        int assetCount = assets.size();
        log_debug("Autoupdate: Found " << assetCount << " assets in release");
        
        // Find the required assets
        for (JsonObject asset : assets) {
            std::string name = asset["name"].as<std::string>();
            std::string downloadUrl = asset["browser_download_url"].as<std::string>();
            size_t assetSize = asset["size"].as<size_t>();
            
            log_debug("Autoupdate: Found asset: " << name << " (" << assetSize << " bytes)");
            
            if (name == "index.html.gz") {
                indexGzUrl = downloadUrl;
                log_info("Autoupdate: Found index.html.gz at: " << indexGzUrl << " (" << assetSize << " bytes)");
            } else if (name == "firmware.bin") {
                firmwareBinUrl = downloadUrl;
                log_info("Autoupdate: Found firmware.bin at: " << firmwareBinUrl << " (" << assetSize << " bytes)");
            }
        }
        
        if (indexGzUrl.empty() || firmwareBinUrl.empty()) {
            log_error("Autoupdate: Required assets not found in release");
            log_error("Autoupdate: index.html.gz found: " << (!indexGzUrl.empty() ? "YES" : "NO"));
            log_error("Autoupdate: firmware.bin found: " << (!firmwareBinUrl.empty() ? "YES" : "NO"));
            _updateInProgress = false;
            return false;
        }
        
        log_info("Autoupdate: Starting file downloads");
        
        // Download index.html.gz to local filesystem
        std::string tempWebUIPath = "/index_temp.html.gz";
        log_info("Autoupdate: Downloading web UI to: " << tempWebUIPath);
        
        if (!downloadFile(indexGzUrl, tempWebUIPath)) {
            log_error("Autoupdate: Failed to download index.html.gz from: " << indexGzUrl);
            _updateInProgress = false;
            return false;
        }
        
        log_info("Autoupdate: Web UI download completed successfully");
        log_debug("Autoupdate: Free heap after web UI download: " << ESP.getFreeHeap() << " bytes");
        
        // Download firmware.bin to SD card
        std::string firmwarePath = "/sdcard/firmware_update.bin";
        log_info("Autoupdate: Downloading firmware to: " << firmwarePath);
        
        if (!downloadFile(firmwareBinUrl, firmwarePath)) {
            log_error("Autoupdate: Failed to download firmware.bin from: " << firmwareBinUrl);
            log_info("Autoupdate: Cleaning up web UI temp file");
            remove(tempWebUIPath.c_str());
            _updateInProgress = false;
            return false;
        }
        
        log_info("Autoupdate: Firmware download completed successfully");
        log_debug("Autoupdate: Free heap after firmware download: " << ESP.getFreeHeap() << " bytes");
        
        // Install web UI first
        log_info("Autoupdate: Installing web UI");
        
        if (!installWebUI(tempWebUIPath)) {
            log_error("Autoupdate: Failed to install web UI");
            log_info("Autoupdate: Cleaning up downloaded files");
            remove(tempWebUIPath.c_str());
            SD.remove(firmwarePath.c_str());
            _updateInProgress = false;
            return false;
        }
        
        log_info("Autoupdate: Web UI installation completed successfully");
        
        // Install firmware (this will trigger a reboot)
        log_info("Autoupdate: Installing firmware - this will reboot the system");
        
        if (!installFirmware(firmwarePath)) {
            log_error("Autoupdate: Failed to install firmware");
            log_info("Autoupdate: Cleaning up firmware file");
            SD.remove(firmwarePath.c_str());
            _updateInProgress = false;
            return false;
        }
        
        // Clean up
        SD.remove(firmwarePath.c_str());
        
        log_info("Autoupdate: Update completed successfully. System will reboot in 3 seconds...");
        
        // Give some time for the log message to be sent
        delay(3000);
        
        // Reboot to apply firmware update
        ESP.restart();
        
        return true;
    }

    bool AutoUpdate::isUpdateAvailable(const std::string& updateUrl, const std::string& currentVersion) {
        log_debug("Autoupdate: Checking if update is available");
        
        std::string latestVersion = getLatestVersionFromGitHub(updateUrl);
        if (latestVersion.empty()) {
            log_error("Autoupdate: Failed to get latest version");
            return false;
        }
        
        return compareVersions(currentVersion, latestVersion);
    }

    void AutoUpdate::checkForUpdate() {
        // Only proceed if autoupdate is enabled and WiFi is in STA mode
        auto config = Machine::MachineConfig::instance();
        if (!config || !config->_maslowAutoUpdate) {
            return; // Silently return if feature is disabled
        }
        
        // ONE-SHOT CHECK: If we've already completed the update check for this session, don't check again
        if (_updateCheckCompleted) {
            return; // Silently return - no need to log repeatedly
        }
        
        if (WiFi.getMode() != WIFI_STA && WiFi.getMode() != WIFI_AP_STA) {
            return; // Silently return until WiFi is in correct mode
        }
        
        if (!WiFi.isConnected()) {
            return; // Silently return until WiFi is connected
        }
        
        uint32_t now = millis();
        
        // Check if we should perform an update check (30 seconds after connection)
        if (_lastUpdateCheck == 0) {
            _lastUpdateCheck = now;
            log_info("Autoupdate: One-shot update check scheduled - will check for updates in " << (UPDATE_CHECK_INTERVAL / 1000) << " seconds");
            return;
        }
        
        if (now - _lastUpdateCheck < UPDATE_CHECK_INTERVAL) {
            return; // Silently wait - no need to log repeatedly
        }
        
        if (_updateInProgress) {
            return; // Update already in progress
        }
        
        // MARK AS COMPLETED IMMEDIATELY - This is a one-shot operation with no retries
        _updateCheckCompleted = true;
        
        log_info("Autoupdate: Starting one-shot update check (no retries)");
        log_info("Autoupdate: Free heap before update check: " << ESP.getFreeHeap() << " bytes");
        
        std::string currentVersion = getCurrentVersion();
        std::string updateUrl = config->_maslowUpdateURL;
        
        log_info("Autoupdate: Current version: " << currentVersion);
        log_info("Autoupdate: Update URL: " << updateUrl);
        
        // Attempt the update check - this is the only attempt
        bool updateCheckSuccess = false;
        try {
            if (isUpdateAvailable(updateUrl, currentVersion)) {
                log_info("Autoupdate: Update available, starting download and installation");
                
                std::string latestVersion = getLatestVersionFromGitHub(updateUrl);
                if (!latestVersion.empty()) {
                    log_info("Autoupdate: Latest version found: " << latestVersion);
                    updateCheckSuccess = downloadAndInstallUpdate(updateUrl, latestVersion);
                    if (!updateCheckSuccess) {
                        log_error("Autoupdate: Download and installation failed - no retries will be attempted");
                    }
                } else {
                    log_error("Autoupdate: Failed to retrieve latest version information - no retries will be attempted");
                }
            } else {
                log_info("Autoupdate: No update available - one-shot check completed");
                updateCheckSuccess = true; // Not finding an update is not a failure
            }
        } catch (...) {
            log_error("Autoupdate: Exception occurred during update check - no retries will be attempted");
            updateCheckSuccess = false;
        }
        
        log_info("Autoupdate: One-shot update check completed, success: " << (updateCheckSuccess ? "true" : "false") << " (no retries)");
    }
    
    void AutoUpdate::resetUpdateCheck() {
        // Reset the one-shot flag to allow a new update check on WiFi reconnection
        _updateCheckCompleted = false;
        _lastUpdateCheck = 0;
        log_debug("Autoupdate: Reset one-shot update check for new WiFi connection");
    }
}

#endif // ENABLE_WIFI