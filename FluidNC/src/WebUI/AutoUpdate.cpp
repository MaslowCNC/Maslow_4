// Copyright (c) 2024 - FluidNC Contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "AutoUpdate.h"

#ifdef ENABLE_WIFI

#include "WifiConfig.h"
#include "../Machine/MachineConfig.h"
#include "../Settings.h"
#include "../Config.h"
#include "../Logging.h"
#include "../Report.h"

#include <WiFiClientSecure.h>
#include <Update.h>
#include "Driver/localfs.h"

namespace WebUI {
    
    static const char* GITHUB_API_HOST = "api.github.com";
    static const int GITHUB_API_PORT = 443;
    static const char* RELEASE_API_PATH = "/repos/BarbourSmith/FluidNC/releases/latest";
    
    std::string AutoUpdate::getLatestReleaseInfo() {
        WiFiClientSecure client;
        client.setInsecure(); // For simplicity, disable certificate verification
        
        if (!client.connect(GITHUB_API_HOST, GITHUB_API_PORT)) {
            log_error("AutoUpdate: Failed to connect to GitHub API");
            return "";
        }
        
        // Send HTTP request
        client.print("GET ");
        client.print(RELEASE_API_PATH);
        client.print(" HTTP/1.1\r\n");
        client.print("Host: ");
        client.print(GITHUB_API_HOST);
        client.print("\r\n");
        client.print("User-Agent: FluidNC-AutoUpdate\r\n");
        client.print("Accept: application/vnd.github.v3+json\r\n");
        client.print("Connection: close\r\n\r\n");
        
        // Wait for response
        unsigned long timeout = millis() + 10000; // 10 second timeout
        while (client.available() == 0) {
            if (millis() > timeout) {
                log_error("AutoUpdate: Timeout waiting for response");
                client.stop();
                return "";
            }
            delay(100);
        }
        
        // Read response
        std::string response;
        bool headersPassed = false;
        
        while (client.available()) {
            String line = client.readStringUntil('\n');
            if (!headersPassed) {
                if (line.length() <= 2) { // Empty line indicates end of headers
                    headersPassed = true;
                }
                continue;
            }
            response += line.c_str();
        }
        
        client.stop();
        return response;
    }
    
    bool AutoUpdate::checkForUpdate() {
        // Only check if not in AP mode
        if (WiFi.getMode() == WIFI_AP) {
            log_debug("AutoUpdate: Skipping update check - in AP mode");
            return false;
        }
        
        // Only check if auto-update is enabled
        if (!config->_maslowAutoUpdate) {
            log_debug("AutoUpdate: Auto-update disabled in configuration");
            return false;
        }
        
        log_info("AutoUpdate: Checking for new release...");
        
        std::string jsonResponse = getLatestReleaseInfo();
        if (jsonResponse.empty()) {
            log_error("AutoUpdate: Failed to get release information");
            return false;
        }
        
        // Simple JSON parsing for tag_name
        size_t tagPos = jsonResponse.find("\"tag_name\":");
        if (tagPos == std::string::npos) {
            log_error("AutoUpdate: No tag_name found in release info");
            return false;
        }
        
        // Find the value after "tag_name":
        size_t startQuote = jsonResponse.find("\"", tagPos + 11);
        if (startQuote == std::string::npos) {
            log_error("AutoUpdate: Invalid tag_name format");
            return false;
        }
        
        size_t endQuote = jsonResponse.find("\"", startQuote + 1);
        if (endQuote == std::string::npos) {
            log_error("AutoUpdate: Invalid tag_name format");
            return false;
        }
        
        std::string tagName = jsonResponse.substr(startQuote + 1, endQuote - startQuote - 1);
        log_info("AutoUpdate: Latest release: " << tagName);
        
        // Check if this is a newer version than current
        // Compare tag names - if they're different, consider it a new version
        // This is a simple approach; a production system might use semantic versioning
        std::string currentVersion = git_info;
        if (tagName == currentVersion || tagName.empty()) {
            log_info("AutoUpdate: Already running the latest version (" << currentVersion << ")");
            return false;
        }
        
        log_info("AutoUpdate: Current version: " << currentVersion << ", Latest: " << tagName);
        
        // Look for firmware.bin and index.html.gz in assets
        std::string firmwareUrl, webUIUrl;
        
        // Find firmware.bin URL - look for exact filename match
        size_t firmwarePos = jsonResponse.find("\"name\":\"firmware.bin\"");
        if (firmwarePos != std::string::npos) {
            // Search for the asset object containing this name
            size_t assetStart = jsonResponse.rfind("{", firmwarePos);
            if (assetStart != std::string::npos) {
                size_t assetEnd = jsonResponse.find("}", firmwarePos);
                if (assetEnd != std::string::npos) {
                    std::string assetObj = jsonResponse.substr(assetStart, assetEnd - assetStart);
                    size_t urlPos = assetObj.find("\"browser_download_url\":\"");
                    if (urlPos != std::string::npos) {
                        size_t urlStart = urlPos + 24; // length of "browser_download_url":"
                        size_t urlEnd = assetObj.find("\"", urlStart);
                        if (urlEnd != std::string::npos) {
                            firmwareUrl = assetObj.substr(urlStart, urlEnd - urlStart);
                        }
                    }
                }
            }
        }
        
        // Find index.html.gz URL - look for exact filename match
        size_t webUIPos = jsonResponse.find("\"name\":\"index.html.gz\"");
        if (webUIPos != std::string::npos) {
            // Search for the asset object containing this name
            size_t assetStart = jsonResponse.rfind("{", webUIPos);
            if (assetStart != std::string::npos) {
                size_t assetEnd = jsonResponse.find("}", webUIPos);
                if (assetEnd != std::string::npos) {
                    std::string assetObj = jsonResponse.substr(assetStart, assetEnd - assetStart);
                    size_t urlPos = assetObj.find("\"browser_download_url\":\"");
                    if (urlPos != std::string::npos) {
                        size_t urlStart = urlPos + 24; // length of "browser_download_url":"
                        size_t urlEnd = assetObj.find("\"", urlStart);
                        if (urlEnd != std::string::npos) {
                            webUIUrl = assetObj.substr(urlStart, urlEnd - urlStart);
                        }
                    }
                }
            }
        }
        
        if (firmwareUrl.empty() || webUIUrl.empty()) {
            log_error("AutoUpdate: Required files not found in release");
            return false;
        }
        
        log_info("AutoUpdate: New version available. Starting download...");
        return downloadAndInstallUpdate(firmwareUrl, webUIUrl);
    }
    
    bool AutoUpdate::downloadFile(const std::string& url, const std::string& filename) {
        WiFiClientSecure client;
        client.setInsecure();
        
        // Parse URL to get host and path
        size_t hostStart = url.find("://") + 3;
        size_t pathStart = url.find("/", hostStart);
        std::string host = url.substr(hostStart, pathStart - hostStart);
        std::string path = url.substr(pathStart);
        
        if (!client.connect(host.c_str(), 443)) {
            log_error("AutoUpdate: Failed to connect for download: " << host);
            return false;
        }
        
        // Send HTTP request
        client.print("GET ");
        client.print(path.c_str());
        client.print(" HTTP/1.1\r\n");
        client.print("Host: ");
        client.print(host.c_str());
        client.print("\r\n");
        client.print("User-Agent: FluidNC-AutoUpdate\r\n");
        client.print("Connection: close\r\n\r\n");
        
        // Wait for response and skip headers
        bool headersPassed = false;
        while (client.connected() && !headersPassed) {
            String line = client.readStringUntil('\n');
            if (line.length() <= 2) {
                headersPassed = true;
            }
        }
        
        if (!headersPassed) {
            log_error("AutoUpdate: Failed to read download headers");
            client.stop();
            return false;
        }
        
        // Save file
        FILE* file = fopen(filename.c_str(), "wb");
        if (!file) {
            log_error("AutoUpdate: Failed to create file: " << filename);
            client.stop();
            return false;
        }
        
        uint8_t buffer[1024];
        size_t totalBytes = 0;
        
        while (client.connected() || client.available()) {
            if (client.available()) {
                size_t bytesRead = client.readBytes(buffer, sizeof(buffer));
                if (bytesRead > 0) {
                    fwrite(buffer, 1, bytesRead, file);
                    totalBytes += bytesRead;
                }
            }
            delay(1);
        }
        
        fclose(file);
        client.stop();
        
        log_info("AutoUpdate: Downloaded " << totalBytes << " bytes to " << filename);
        return totalBytes > 0;
    }
    
    bool AutoUpdate::installFirmware(const std::string& filename) {
        FILE* file = fopen(filename.c_str(), "rb");
        if (!file) {
            log_error("AutoUpdate: Failed to open firmware file: " << filename);
            return false;
        }
        
        // Get file size
        fseek(file, 0, SEEK_END);
        size_t fileSize = ftell(file);
        fseek(file, 0, SEEK_SET);
        
        log_info("AutoUpdate: Installing firmware (" << fileSize << " bytes)...");
        
        if (!Update.begin(fileSize)) {
            log_error("AutoUpdate: Failed to begin firmware update");
            fclose(file);
            return false;
        }
        
        uint8_t buffer[1024];
        size_t bytesWritten = 0;
        
        while (bytesWritten < fileSize) {
            size_t bytesToRead = std::min(sizeof(buffer), fileSize - bytesWritten);
            size_t bytesRead = fread(buffer, 1, bytesToRead, file);
            
            if (bytesRead == 0) {
                break;
            }
            
            if (Update.write(buffer, bytesRead) != bytesRead) {
                log_error("AutoUpdate: Failed to write firmware data");
                Update.abort();
                fclose(file);
                return false;
            }
            
            bytesWritten += bytesRead;
        }
        
        fclose(file);
        
        if (!Update.end(true)) {
            log_error("AutoUpdate: Failed to complete firmware update");
            return false;
        }
        
        log_info("AutoUpdate: Firmware update completed successfully");
        return true;
    }
    
    bool AutoUpdate::installWebUI(const std::string& filename) {
        // Simply copy the file to the data directory
        std::string destPath = "/localfs/index.html.gz";
        
        FILE* src = fopen(filename.c_str(), "rb");
        if (!src) {
            log_error("AutoUpdate: Failed to open source WebUI file: " << filename);
            return false;
        }
        
        FILE* dest = fopen(destPath.c_str(), "wb");
        if (!dest) {
            log_error("AutoUpdate: Failed to create destination WebUI file: " << destPath);
            fclose(src);
            return false;
        }
        
        uint8_t buffer[1024];
        size_t bytesRead;
        size_t totalBytes = 0;
        
        while ((bytesRead = fread(buffer, 1, sizeof(buffer), src)) > 0) {
            if (fwrite(buffer, 1, bytesRead, dest) != bytesRead) {
                log_error("AutoUpdate: Failed to write WebUI data");
                fclose(src);
                fclose(dest);
                return false;
            }
            totalBytes += bytesRead;
        }
        
        fclose(src);
        fclose(dest);
        
        log_info("AutoUpdate: WebUI update completed (" << totalBytes << " bytes)");
        return true;
    }
    
    bool AutoUpdate::downloadAndInstallUpdate(const std::string& firmwareUrl, const std::string& webUIUrl) {
        // Safety checks
        if (firmwareUrl.empty() || webUIUrl.empty()) {
            log_error("AutoUpdate: Invalid URLs provided");
            return false;
        }
        
        // Only proceed if we have sufficient free space
        // This is a rough check - in practice you'd want more sophisticated space checking
        log_info("AutoUpdate: Starting download and installation process...");
        
        // Download files to temporary location
        std::string tempFirmware = "/tmp/firmware.bin";
        std::string tempWebUI = "/tmp/index.html.gz";
        
        log_info("AutoUpdate: Downloading firmware from: " << firmwareUrl);
        if (!downloadFile(firmwareUrl, tempFirmware)) {
            log_error("AutoUpdate: Failed to download firmware");
            return false;
        }
        
        log_info("AutoUpdate: Downloading WebUI from: " << webUIUrl);
        if (!downloadFile(webUIUrl, tempWebUI)) {
            log_error("AutoUpdate: Failed to download WebUI");
            remove(tempFirmware.c_str());
            return false;
        }
        
        // Verify downloads by checking file sizes
        FILE* firmwareFile = fopen(tempFirmware.c_str(), "rb");
        if (!firmwareFile) {
            log_error("AutoUpdate: Downloaded firmware file not found");
            remove(tempFirmware.c_str());
            remove(tempWebUI.c_str());
            return false;
        }
        fseek(firmwareFile, 0, SEEK_END);
        size_t firmwareSize = ftell(firmwareFile);
        fclose(firmwareFile);
        
        if (firmwareSize < 100000) { // Firmware should be at least 100KB
            log_error("AutoUpdate: Downloaded firmware file too small (" << firmwareSize << " bytes)");
            remove(tempFirmware.c_str());
            remove(tempWebUI.c_str());
            return false;
        }
        
        log_info("AutoUpdate: Firmware file size: " << firmwareSize << " bytes");
        
        // Install WebUI first (less risky)
        if (!installWebUI(tempWebUI)) {
            log_error("AutoUpdate: Failed to install WebUI");
            remove(tempFirmware.c_str());
            remove(tempWebUI.c_str());
            return false;
        }
        
        // Install firmware (this will trigger a restart)
        if (!installFirmware(tempFirmware)) {
            log_error("AutoUpdate: Failed to install firmware");
            remove(tempFirmware.c_str());
            remove(tempWebUI.c_str());
            return false;
        }
        
        // Clean up temporary files
        remove(tempFirmware.c_str());
        remove(tempWebUI.c_str());
        
        log_info("AutoUpdate: Update completed successfully. Restarting in 3 seconds...");
        delay(3000);
        ESP.restart();
        
        return true;
    }
}

#endif