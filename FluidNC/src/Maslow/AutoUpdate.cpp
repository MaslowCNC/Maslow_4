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
            log_error("AutoUpdate: Failed to resolve download URL");
            return false;
        }
        
        http.begin(client, finalUrl.c_str());
        http.addHeader("User-Agent", "FluidNC-AutoUpdate/1.0");
        http.setTimeout(30000); // 30 second timeout for downloads
        
        int httpCode = http.GET();
        log_debug("AutoUpdate: Download response code: " << httpCode);
        
        if (httpCode == HTTP_CODE_OK) {
            int contentLength = http.getSize();
            log_debug("AutoUpdate: Content length: " << contentLength);
            
            // Determine the appropriate file system
            FILE* file = nullptr;
            if (filePath.find("/sdcard/") == 0) {
                // SD card file
                if (!SD.exists("/")) {
                    log_error("AutoUpdate: SD card not available");
                    http.end();
                    return false;
                }
                file = fopen(filePath.c_str(), "wb");
            } else {
                // Local filesystem file
                file = fopen(filePath.c_str(), "wb");
            }
            
            if (!file) {
                log_error("AutoUpdate: Failed to create file: " << filePath);
                http.end();
                return false;
            }
            
            WiFiClient* stream = http.getStreamPtr();
            size_t bytesDownloaded = 0;
            uint8_t buffer[1024];
            
            while (http.connected() && bytesDownloaded < contentLength) {
                int bytesRead = stream->readBytes(buffer, sizeof(buffer));
                if (bytesRead > 0) {
                    fwrite(buffer, 1, bytesRead, file);
                    bytesDownloaded += bytesRead;
                    
                    if (bytesDownloaded % 10240 == 0) { // Log every 10KB
                        log_debug("AutoUpdate: Downloaded " << bytesDownloaded << " / " << contentLength << " bytes");
                    }
                }
                delay(1); // Yield to other tasks
            }
            
            fclose(file);
            http.end();
            
            log_info("AutoUpdate: Download completed. Total bytes: " << bytesDownloaded);
            
            if (expectedSize > 0 && bytesDownloaded != expectedSize) {
                log_error("AutoUpdate: Download size mismatch. Expected: " << expectedSize << ", got: " << bytesDownloaded);
                return false;
            }
            
            return true;
        } else {
            log_error("AutoUpdate: Download failed with HTTP code: " << httpCode);
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
        
        if (_updateInProgress) {
            log_warn("AutoUpdate: Update already in progress");
            return false;
        }
        
        _updateInProgress = true;
        
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
            }
        }
        
        log_debug("AutoUpdate: Release API URL: " << apiUrl);
        
        http.begin(client, apiUrl.c_str());
        http.addHeader("User-Agent", "FluidNC-AutoUpdate/1.0");
        
        int httpCode = http.GET();
        if (httpCode != HTTP_CODE_OK) {
            log_error("AutoUpdate: Failed to get release information, HTTP code: " << httpCode);
            _updateInProgress = false;
            http.end();
            return false;
        }
        
        String payload = http.getString();
        http.end();
        
        DynamicJsonDocument doc(16384);
        DeserializationError error = deserializeJson(doc, payload);
        
        if (error) {
            log_error("AutoUpdate: Failed to parse release information: " << error.c_str());
            _updateInProgress = false;
            return false;
        }
        
        JsonArray assets = doc["assets"];
        std::string indexGzUrl = "";
        std::string firmwareBinUrl = "";
        
        // Find the required assets
        for (JsonObject asset : assets) {
            std::string name = asset["name"].as<std::string>();
            std::string downloadUrl = asset["browser_download_url"].as<std::string>();
            
            if (name == "index.html.gz") {
                indexGzUrl = downloadUrl;
                log_debug("AutoUpdate: Found index.html.gz at: " << indexGzUrl);
            } else if (name == "firmware.bin") {
                firmwareBinUrl = downloadUrl;
                log_debug("AutoUpdate: Found firmware.bin at: " << firmwareBinUrl);
            }
        }
        
        if (indexGzUrl.empty() || firmwareBinUrl.empty()) {
            log_error("AutoUpdate: Required assets not found in release");
            _updateInProgress = false;
            return false;
        }
        
        // Download index.html.gz to local filesystem
        std::string tempWebUIPath = "/index_temp.html.gz";
        if (!downloadFile(indexGzUrl, tempWebUIPath)) {
            log_error("AutoUpdate: Failed to download index.html.gz");
            _updateInProgress = false;
            return false;
        }
        
        // Download firmware.bin to SD card
        std::string firmwarePath = "/sdcard/firmware_update.bin";
        if (!downloadFile(firmwareBinUrl, firmwarePath)) {
            log_error("AutoUpdate: Failed to download firmware.bin");
            remove(tempWebUIPath.c_str());
            _updateInProgress = false;
            return false;
        }
        
        // Install web UI first
        if (!installWebUI(tempWebUIPath)) {
            log_error("AutoUpdate: Failed to install web UI");
            remove(tempWebUIPath.c_str());
            SD.remove(firmwarePath.c_str());
            _updateInProgress = false;
            return false;
        }
        
        // Install firmware (this will trigger a reboot)
        if (!installFirmware(firmwarePath)) {
            log_error("AutoUpdate: Failed to install firmware");
            SD.remove(firmwarePath.c_str());
            _updateInProgress = false;
            return false;
        }
        
        // Clean up
        SD.remove(firmwarePath.c_str());
        
        log_info("AutoUpdate: Update completed successfully. System will reboot.");
        
        // Reboot to apply firmware update
        delay(1000);
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
            return;
        }
        
        if (WiFi.getMode() != WIFI_STA && WiFi.getMode() != WIFI_AP_STA) {
            log_debug("AutoUpdate: WiFi not in STA mode, skipping update check");
            return;
        }
        
        if (!WiFi.isConnected()) {
            log_debug("AutoUpdate: WiFi not connected, skipping update check");
            return;
        }
        
        uint32_t now = millis();
        
        // Check if we should perform an update check (30 seconds after connection)
        if (_lastUpdateCheck == 0) {
            _lastUpdateCheck = now;
            log_debug("AutoUpdate: Marking initial connection time");
            return;
        }
        
        if (now - _lastUpdateCheck < UPDATE_CHECK_INTERVAL) {
            return; // Not time yet
        }
        
        if (_updateInProgress) {
            return; // Update already in progress
        }
        
        // Reset check time to prevent repeated checks
        _lastUpdateCheck = now + 86400000; // Next check in 24 hours
        
        log_info("AutoUpdate: Starting update check");
        
        std::string currentVersion = getCurrentVersion();
        std::string updateUrl = config->_maslowUpdateURL;
        
        log_info("AutoUpdate: Current version: " << currentVersion);
        log_info("AutoUpdate: Update URL: " << updateUrl);
        
        if (isUpdateAvailable(updateUrl, currentVersion)) {
            log_info("AutoUpdate: Update available, starting download and installation");
            
            std::string latestVersion = getLatestVersionFromGitHub(updateUrl);
            if (!latestVersion.empty()) {
                downloadAndInstallUpdate(updateUrl, latestVersion);
            }
        } else {
            log_info("AutoUpdate: No update available");
        }
    }
}

#endif // ENABLE_WIFI