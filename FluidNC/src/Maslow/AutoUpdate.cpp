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
    uint32_t AutoUpdate::_lastFailedUpdate = 0;
    bool AutoUpdate::_updateCheckCompleted = false;

    std::string AutoUpdate::getCurrentVersion() {
        // Extract version from git_info (format: "v3.7.x (Maslow-Main-abc123)")
        std::string version = git_info;
        log_debug("AutoUpdate: Current version string: " << version);
        
        size_t space_pos = version.find(' ');
        if (space_pos != std::string::npos) {
            version = version.substr(0, space_pos);
        }
        
        log_debug("AutoUpdate: Parsed current version: " << version);
        return version;
    }

    bool AutoUpdate::compareVersions(const std::string& current, const std::string& latest) {
        log_debug("AutoUpdate: Comparing versions - current: '" << current << "', latest: '" << latest << "'");
        
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
        log_debug("AutoUpdate: Version comparison result - newer version available: " << (isNewer ? "true" : "false"));
        return isNewer;
    }

    std::string AutoUpdate::followRedirects(HTTPClient& http, const std::string& url, int maxRedirects) {
        log_debug("AutoUpdate: Following redirects for URL: " << url << ", max redirects: " << maxRedirects);
        
        std::string currentUrl = url;
        
        for (int i = 0; i < maxRedirects; i++) {
            http.begin(currentUrl.c_str());
            http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
            
            int httpCode = http.GET();
            log_debug("AutoUpdate: HTTP response code: " << httpCode << " for URL: " << currentUrl);
            
            if (httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND || httpCode == HTTP_CODE_TEMPORARY_REDIRECT) {
                String location = http.getLocation();
                http.end();
                
                if (location.length() > 0) {
                    currentUrl = location.c_str();
                    log_debug("AutoUpdate: Redirected to: " << currentUrl);
                } else {
                    log_error("AutoUpdate: Redirect response but no Location header");
                    return "";
                }
            } else if (httpCode == HTTP_CODE_OK) {
                log_debug("AutoUpdate: Final URL after redirects: " << currentUrl);
                http.end();
                return currentUrl;
            } else {
                log_error("AutoUpdate: HTTP error: " << httpCode);
                http.end();
                return "";
            }
        }
        
        log_error("AutoUpdate: Too many redirects");
        return "";
    }

    std::string AutoUpdate::getLatestVersionFromGitHub(const std::string& updateUrl) {
        log_info("AutoUpdate: Checking for latest version from: " << updateUrl);
        
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
        
        log_debug("AutoUpdate: API URL: " << apiUrl);
        
        http.begin(client, apiUrl.c_str());
        http.addHeader("User-Agent", "FluidNC-AutoUpdate/1.0");
        http.setTimeout(10000); // 10 second timeout
        
        int httpCode = http.GET();
        log_debug("AutoUpdate: GitHub API response code: " << httpCode);
        
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            log_debug("AutoUpdate: GitHub API response length: " << payload.length());
            
            // Parse JSON response
            DynamicJsonDocument doc(8192);
            DeserializationError error = deserializeJson(doc, payload);
            
            if (error) {
                log_error("AutoUpdate: Failed to parse GitHub API response: " << error.c_str());
                http.end();
                return "";
            }
            
            if (doc.containsKey("tag_name")) {
                std::string latestVersion = doc["tag_name"].as<std::string>();
                log_info("AutoUpdate: Latest version found: " << latestVersion);
                http.end();
                return latestVersion;
            } else {
                log_error("AutoUpdate: GitHub API response missing tag_name field");
            }
        } else {
            log_error("AutoUpdate: Failed to fetch latest version, HTTP code: " << httpCode);
        }
        
        http.end();
        return "";
    }

    bool AutoUpdate::downloadFile(const std::string& url, const std::string& filePath, size_t expectedSize) {
        log_info("AutoUpdate: Downloading file from: " << url << " to: " << filePath);
        
        HTTPClient http;
        WiFiClientSecure client;
        client.setInsecure();
        
        // Follow redirects to get the actual download URL
        std::string finalUrl = followRedirects(http, url, 5);
        if (finalUrl.empty()) {
            log_error("AutoUpdate: Failed to resolve download URL, starting URL was: " << url);
            return false;
        }
        
        if (finalUrl != url) {
            log_debug("AutoUpdate: Following redirects from " << url << " to " << finalUrl);
        }
        
        log_debug("AutoUpdate: Starting download from: " << finalUrl);
        
        http.begin(client, finalUrl.c_str());
        http.addHeader("User-Agent", "FluidNC-AutoUpdate/1.0");
        http.setTimeout(60000); // 60 second timeout for downloads
        
        int httpCode = http.GET();
        log_debug("AutoUpdate: Download HTTP response code: " << httpCode);
        
        if (httpCode == HTTP_CODE_OK) {
            int contentLength = http.getSize();
            log_info("AutoUpdate: Content length: " << contentLength << " bytes");
            
            if (expectedSize > 0 && expectedSize != contentLength) {
                log_warn("AutoUpdate: Expected size (" << expectedSize << ") differs from actual size (" << contentLength << ")");
            }
            
            // Determine the appropriate file system
            FILE* file = nullptr;
            if (filePath.find("/sdcard/") == 0) {
                // SD card file
                if (!SD.exists("/")) {
                    log_error("AutoUpdate: SD card not available for file: " << filePath);
                    http.end();
                    return false;
                }
                log_debug("AutoUpdate: Writing to SD card: " << filePath);
                file = fopen(filePath.c_str(), "wb");
            } else {
                // Local filesystem file
                log_debug("AutoUpdate: Writing to local filesystem: " << filePath);
                file = fopen(filePath.c_str(), "wb");
            }
            
            if (!file) {
                log_error("AutoUpdate: Failed to create file: " << filePath << " (errno: " << errno << ")");
                http.end();
                return false;
            }
            
            WiFiClient* stream = http.getStreamPtr();
            size_t bytesDownloaded = 0;
            uint8_t buffer[1024];
            uint32_t lastProgressLog = millis();
            
            log_info("AutoUpdate: Starting file transfer");
            
            while (http.connected() && bytesDownloaded < contentLength) {
                int bytesRead = stream->readBytes(buffer, sizeof(buffer));
                if (bytesRead > 0) {
                    size_t bytesWritten = fwrite(buffer, 1, bytesRead, file);
                    if (bytesWritten != bytesRead) {
                        log_error("AutoUpdate: Write error, expected " << bytesRead << " bytes, wrote " << bytesWritten);
                        fclose(file);
                        http.end();
                        return false;
                    }
                    bytesDownloaded += bytesRead;
                    
                    // Log progress every 10KB or every 5 seconds
                    uint32_t now = millis();
                    if (bytesDownloaded % 10240 == 0 || (now - lastProgressLog) > 5000) {
                        int percent = (bytesDownloaded * 100) / contentLength;
                        log_info("AutoUpdate: Downloaded " << bytesDownloaded << " / " << contentLength << " bytes (" << percent << "%)");
                        lastProgressLog = now;
                    }
                } else if (bytesRead == 0) {
                    log_debug("AutoUpdate: No bytes read, stream may be finished or blocked");
                    delay(10); // Small delay to prevent tight loop
                } else {
                    log_error("AutoUpdate: Read error: " << bytesRead);
                    break;
                }
                delay(1); // Yield to other tasks
            }
            
            fclose(file);
            http.end();
            
            log_info("AutoUpdate: Download completed. Total bytes: " << bytesDownloaded << " / " << contentLength);
            
            if (bytesDownloaded != contentLength) {
                log_error("AutoUpdate: Download incomplete. Expected: " << contentLength << ", got: " << bytesDownloaded);
                return false;
            }
            
            if (expectedSize > 0 && bytesDownloaded != expectedSize) {
                log_error("AutoUpdate: Download size mismatch. Expected: " << expectedSize << ", got: " << bytesDownloaded);
                return false;
            }
            
            log_info("AutoUpdate: File download successful: " << filePath);
            return true;
        } else {
            log_error("AutoUpdate: Download failed with HTTP code: " << httpCode << " for URL: " << finalUrl);
            http.end();
            return false;
        }
    }

    bool AutoUpdate::validateFile(const std::string& filePath, size_t expectedSize) {
        log_debug("AutoUpdate: Validating file: " << filePath);
        
        FILE* file = fopen(filePath.c_str(), "rb");
        if (!file) {
            log_error("AutoUpdate: Cannot open file for validation: " << filePath);
            return false;
        }
        
        fseek(file, 0, SEEK_END);
        size_t fileSize = ftell(file);
        fclose(file);
        
        log_debug("AutoUpdate: File size: " << fileSize << ", expected: " << expectedSize);
        
        if (expectedSize > 0 && fileSize != expectedSize) {
            log_error("AutoUpdate: File size validation failed");
            return false;
        }
        
        log_debug("AutoUpdate: File validation passed");
        return true;
    }

    bool AutoUpdate::installWebUI(const std::string& webUIPath) {
        log_info("AutoUpdate: Installing web UI from: " << webUIPath);
        
        // Remove old index.html.gz if it exists
        if (remove("/index.html.gz") == 0) {
            log_debug("AutoUpdate: Removed old index.html.gz");
        }
        
        // Copy downloaded file to final location
        FILE* sourceFile = fopen(webUIPath.c_str(), "rb");
        if (!sourceFile) {
            log_error("AutoUpdate: Cannot open downloaded web UI file");
            return false;
        }
        
        FILE* destFile = fopen("/index.html.gz", "wb");
        if (!destFile) {
            log_error("AutoUpdate: Cannot create final web UI file");
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
        
        log_info("AutoUpdate: Web UI installation completed");
        return true;
    }

    bool AutoUpdate::installFirmware(const std::string& firmwarePath) {
        log_info("AutoUpdate: Installing firmware from: " << firmwarePath);
        
        File firmwareFile = SD.open(firmwarePath.c_str(), FILE_READ);
        if (!firmwareFile) {
            log_error("AutoUpdate: Cannot open firmware file");
            return false;
        }
        
        size_t firmwareSize = firmwareFile.size();
        log_debug("AutoUpdate: Firmware size: " << firmwareSize);
        
        if (!Update.begin(firmwareSize)) {
            log_error("AutoUpdate: Cannot start firmware update. Error: " << Update.errorString());
            firmwareFile.close();
            return false;
        }
        
        Update.onProgress([](size_t progress, size_t total) {
            if (progress % 65536 == 0) { // Log every 64KB
                log_debug("AutoUpdate: Firmware update progress: " << (progress * 100 / total) << "%");
            }
        });
        
        size_t written = Update.writeStream(firmwareFile);
        firmwareFile.close();
        
        if (written != firmwareSize) {
            log_error("AutoUpdate: Firmware write failed. Written: " << written << ", expected: " << firmwareSize);
            Update.abort();
            return false;
        }
        
        if (!Update.end()) {
            log_error("AutoUpdate: Firmware update end failed. Error: " << Update.errorString());
            return false;
        }
        
        log_info("AutoUpdate: Firmware update completed successfully");
        return true;
    }

    bool AutoUpdate::downloadAndInstallUpdate(const std::string& updateUrl, const std::string& newVersion) {
        log_info("AutoUpdate: Starting download and installation for version: " << newVersion);
        log_debug("AutoUpdate: Free heap at start: " << ESP.getFreeHeap() << " bytes");
        
        if (_updateInProgress) {
            log_warn("AutoUpdate: Update already in progress");
            return false;
        }
        
        _updateInProgress = true;
        log_debug("AutoUpdate: Set update in progress flag");
        
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
                log_debug("AutoUpdate: Converted to API URL: " << apiUrl);
            } else {
                log_error("AutoUpdate: Invalid GitHub URL format - missing /releases");
                _updateInProgress = false;
                return false;
            }
        } else {
            log_error("AutoUpdate: Not a GitHub URL: " << updateUrl);
            _updateInProgress = false;
            return false;
        }
        
        log_info("AutoUpdate: Fetching release information from GitHub API");
        
        http.begin(client, apiUrl.c_str());
        http.addHeader("User-Agent", "FluidNC-AutoUpdate/1.0");
        http.setTimeout(15000); // 15 second timeout
        
        int httpCode = http.GET();
        log_debug("AutoUpdate: GitHub API HTTP response code: " << httpCode);
        
        if (httpCode != HTTP_CODE_OK) {
            log_error("AutoUpdate: Failed to get release information, HTTP code: " << httpCode << ", URL: " << apiUrl);
            _updateInProgress = false;
            http.end();
            return false;
        }
        
        String payload = http.getString();
        int payloadLength = payload.length();
        http.end();
        
        log_debug("AutoUpdate: Received " << payloadLength << " bytes from GitHub API");
        log_debug("AutoUpdate: Free heap after API call: " << ESP.getFreeHeap() << " bytes");
        
        DynamicJsonDocument doc(16384);
        DeserializationError error = deserializeJson(doc, payload);
        
        if (error) {
            log_error("AutoUpdate: Failed to parse release information: " << error.c_str());
            log_debug("AutoUpdate: JSON payload preview: " << payload.substring(0, 200).c_str());
            _updateInProgress = false;
            return false;
        }
        
        log_info("AutoUpdate: Successfully parsed release information");
        
        JsonArray assets = doc["assets"];
        std::string indexGzUrl = "";
        std::string firmwareBinUrl = "";
        
        int assetCount = assets.size();
        log_debug("AutoUpdate: Found " << assetCount << " assets in release");
        
        // Find the required assets
        for (JsonObject asset : assets) {
            std::string name = asset["name"].as<std::string>();
            std::string downloadUrl = asset["browser_download_url"].as<std::string>();
            size_t assetSize = asset["size"].as<size_t>();
            
            log_debug("AutoUpdate: Found asset: " << name << " (" << assetSize << " bytes)");
            
            if (name == "index.html.gz") {
                indexGzUrl = downloadUrl;
                log_info("AutoUpdate: Found index.html.gz at: " << indexGzUrl << " (" << assetSize << " bytes)");
            } else if (name == "firmware.bin") {
                firmwareBinUrl = downloadUrl;
                log_info("AutoUpdate: Found firmware.bin at: " << firmwareBinUrl << " (" << assetSize << " bytes)");
            }
        }
        
        if (indexGzUrl.empty() || firmwareBinUrl.empty()) {
            log_error("AutoUpdate: Required assets not found in release");
            log_error("AutoUpdate: index.html.gz found: " << (!indexGzUrl.empty() ? "YES" : "NO"));
            log_error("AutoUpdate: firmware.bin found: " << (!firmwareBinUrl.empty() ? "YES" : "NO"));
            _updateInProgress = false;
            return false;
        }
        
        log_info("AutoUpdate: Starting file downloads");
        
        // Download index.html.gz to local filesystem
        std::string tempWebUIPath = "/index_temp.html.gz";
        log_info("AutoUpdate: Downloading web UI to: " << tempWebUIPath);
        
        if (!downloadFile(indexGzUrl, tempWebUIPath)) {
            log_error("AutoUpdate: Failed to download index.html.gz from: " << indexGzUrl);
            _updateInProgress = false;
            return false;
        }
        
        log_info("AutoUpdate: Web UI download completed successfully");
        log_debug("AutoUpdate: Free heap after web UI download: " << ESP.getFreeHeap() << " bytes");
        
        // Download firmware.bin to SD card
        std::string firmwarePath = "/sdcard/firmware_update.bin";
        log_info("AutoUpdate: Downloading firmware to: " << firmwarePath);
        
        if (!downloadFile(firmwareBinUrl, firmwarePath)) {
            log_error("AutoUpdate: Failed to download firmware.bin from: " << firmwareBinUrl);
            log_info("AutoUpdate: Cleaning up web UI temp file");
            remove(tempWebUIPath.c_str());
            _updateInProgress = false;
            return false;
        }
        
        log_info("AutoUpdate: Firmware download completed successfully");
        log_debug("AutoUpdate: Free heap after firmware download: " << ESP.getFreeHeap() << " bytes");
        
        // Install web UI first
        log_info("AutoUpdate: Installing web UI");
        
        if (!installWebUI(tempWebUIPath)) {
            log_error("AutoUpdate: Failed to install web UI");
            log_info("AutoUpdate: Cleaning up downloaded files");
            remove(tempWebUIPath.c_str());
            SD.remove(firmwarePath.c_str());
            _updateInProgress = false;
            return false;
        }
        
        log_info("AutoUpdate: Web UI installation completed successfully");
        
        // Install firmware (this will trigger a reboot)
        log_info("AutoUpdate: Installing firmware - this will reboot the system");
        
        if (!installFirmware(firmwarePath)) {
            log_error("AutoUpdate: Failed to install firmware");
            log_info("AutoUpdate: Cleaning up firmware file");
            SD.remove(firmwarePath.c_str());
            _updateInProgress = false;
            return false;
        }
        
        // Clean up
        SD.remove(firmwarePath.c_str());
        
        log_info("AutoUpdate: Update completed successfully. System will reboot in 3 seconds...");
        
        // Give some time for the log message to be sent
        delay(3000);
        
        // Reboot to apply firmware update
        ESP.restart();
        
        return true;
    }

    bool AutoUpdate::isUpdateAvailable(const std::string& updateUrl, const std::string& currentVersion) {
        log_debug("AutoUpdate: Checking if update is available");
        
        std::string latestVersion = getLatestVersionFromGitHub(updateUrl);
        if (latestVersion.empty()) {
            log_error("AutoUpdate: Failed to get latest version");
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
        
        // Check if we recently failed an update (prevent retry loops)
        if (_lastFailedUpdate != 0 && (now - _lastFailedUpdate) < FAILED_UPDATE_RETRY_INTERVAL) {
            if (!_updateCheckCompleted) {
                uint32_t timeLeft = FAILED_UPDATE_RETRY_INTERVAL - (now - _lastFailedUpdate);
                log_info("AutoUpdate: Skipping check due to recent failure, retry in " << (timeLeft / 3600000) << " hours");
                _updateCheckCompleted = true; // Mark as completed so we don't log this repeatedly
            }
            return;
        }
        
        // Check if we should perform an update check (30 seconds after connection)
        if (_lastUpdateCheck == 0) {
            _lastUpdateCheck = now;
            log_info("AutoUpdate: One-shot update check scheduled - will check for updates in " << (UPDATE_CHECK_INTERVAL / 1000) << " seconds");
            return;
        }
        
        if (now - _lastUpdateCheck < UPDATE_CHECK_INTERVAL) {
            return; // Silently wait - no need to log repeatedly
        }
        
        if (_updateInProgress) {
            return; // Update already in progress
        }
        
        // MARK AS COMPLETED IMMEDIATELY - This is a one-shot operation
        _updateCheckCompleted = true;
        
        log_info("AutoUpdate: Starting one-shot update check");
        log_info("AutoUpdate: Free heap before update check: " << ESP.getFreeHeap() << " bytes");
        
        std::string currentVersion = getCurrentVersion();
        std::string updateUrl = config->_maslowUpdateURL;
        
        log_info("AutoUpdate: Current version: " << currentVersion);
        log_info("AutoUpdate: Update URL: " << updateUrl);
        
        // Attempt the update check
        bool updateCheckSuccess = false;
        try {
            if (isUpdateAvailable(updateUrl, currentVersion)) {
                log_info("AutoUpdate: Update available, starting download and installation");
                
                std::string latestVersion = getLatestVersionFromGitHub(updateUrl);
                if (!latestVersion.empty()) {
                    log_info("AutoUpdate: Latest version found: " << latestVersion);
                    updateCheckSuccess = downloadAndInstallUpdate(updateUrl, latestVersion);
                    if (!updateCheckSuccess) {
                        log_error("AutoUpdate: Download and installation failed");
                    }
                } else {
                    log_error("AutoUpdate: Failed to retrieve latest version information");
                }
            } else {
                log_info("AutoUpdate: No update available - one-shot check completed");
                updateCheckSuccess = true; // Not finding an update is not a failure
            }
        } catch (...) {
            log_error("AutoUpdate: Exception occurred during update check");
            updateCheckSuccess = false;
        }
        
        // Track failed attempts for future sessions (but don't retry this session)
        if (!updateCheckSuccess) {
            _lastFailedUpdate = now;
            log_error("AutoUpdate: One-shot update attempt failed - will not retry until next boot or 24 hours pass");
        } else {
            _lastFailedUpdate = 0; // Clear failure flag on success
        }
        
        log_info("AutoUpdate: One-shot update check completed, success: " << (updateCheckSuccess ? "true" : "false"));
    }
    
    void AutoUpdate::resetUpdateCheck() {
        // Reset the one-shot flag to allow a new update check on WiFi reconnection
        _updateCheckCompleted = false;
        _lastUpdateCheck = 0;
        log_debug("AutoUpdate: Reset one-shot update check for new WiFi connection");
    }
}

#endif // ENABLE_WIFI