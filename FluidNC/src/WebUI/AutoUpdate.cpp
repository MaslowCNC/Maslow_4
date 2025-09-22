// Copyright (c) 2024 - FluidNC Contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "AutoUpdate.h"

#ifdef ENABLE_WIFI

#    include "WifiConfig.h"
#    include "../Machine/MachineConfig.h"
#    include "../Settings.h"
#    include "../Config.h"
#    include "../Logging.h"
#    include "../Report.h"

#    include <WiFiClientSecure.h>
#    include <Update.h>
#    include "Driver/localfs.h"

namespace WebUI {

    static const int HTTPS_PORT = 443;

    std::string AutoUpdate::getLatestReleaseInfo() {
        WiFiClientSecure client;
        client.setInsecure();  // For simplicity, disable certificate verification

        // Get the configurable update URL
        std::string updateURL = config->_maslowUpdateURL;
        
        // Parse URL to extract host and path
        size_t hostStart = updateURL.find("://") + 3;
        size_t pathStart = updateURL.find("/", hostStart);
        std::string host = updateURL.substr(hostStart, pathStart - hostStart);
        std::string path = updateURL.substr(pathStart);

        if (!client.connect(host.c_str(), HTTPS_PORT)) {
            log_error("AutoUpdate: Failed to connect to " << host);
            return "";
        }

        // Send HTTP request
        client.print("GET ");
        client.print(path.c_str());
        client.print(" HTTP/1.1\r\n");
        client.print("Host: ");
        client.print(host.c_str());
        client.print("\r\n");
        client.print("User-Agent: FluidNC-AutoUpdate\r\n");
        client.print("Accept: application/vnd.github.v3+json\r\n");
        client.print("Connection: close\r\n\r\n");

        // Wait for response
        unsigned long timeout = millis() + 10000;  // 10 second timeout
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
        bool        headersPassed = false;

        while (client.available()) {
            String line = client.readStringUntil('\n');
            if (!headersPassed) {
                if (line.length() <= 2) {  // Empty line indicates end of headers
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

        // Only proceed with download/install if auto-update is enabled
        if (!config->_maslowAutoUpdate) {
            log_info("AutoUpdate: New version available but auto-update is disabled in configuration");
            return false;
        }

        // Look for firmware.bin and index.html.gz in assets
        std::string firmwareUrl, webUIUrl;

        // Debug: Log all available assets
        size_t assetsPos = jsonResponse.find("\"assets\":");
        if (assetsPos != std::string::npos) {
            log_info("AutoUpdate: Searching for assets in release...");
            size_t namePos = assetsPos;
            while ((namePos = jsonResponse.find("\"name\":\"", namePos + 1)) != std::string::npos) {
                size_t nameStart = namePos + 8; // length of "name":"
                size_t nameEnd = jsonResponse.find("\"", nameStart);
                if (nameEnd != std::string::npos) {
                    std::string assetName = jsonResponse.substr(nameStart, nameEnd - nameStart);
                    log_info("AutoUpdate: Found asset: " << assetName);
                }
            }
        }

        // Find firmware.bin URL - look for exact filename match
        size_t firmwarePos = jsonResponse.find("\"name\":\"firmware.bin\"");
        if (firmwarePos != std::string::npos) {
            log_info("AutoUpdate: Searching for firmware.bin download URL...");
            // Search for the asset object containing this name
            size_t assetStart = jsonResponse.rfind("{", firmwarePos);
            if (assetStart != std::string::npos) {
                // Find the matching closing brace by counting braces
                size_t assetEnd = assetStart + 1;
                int braceCount = 1;
                while (assetEnd < jsonResponse.length() && braceCount > 0) {
                    if (jsonResponse[assetEnd] == '{') {
                        braceCount++;
                    } else if (jsonResponse[assetEnd] == '}') {
                        braceCount--;
                    }
                    if (braceCount > 0) assetEnd++;
                }
                
                if (braceCount == 0) {
                    std::string assetObj = jsonResponse.substr(assetStart, assetEnd - assetStart + 1);
                    log_info("AutoUpdate: Extracted asset object for firmware.bin");
                    size_t urlPos = assetObj.find("\"browser_download_url\":\"");
                    if (urlPos != std::string::npos) {
                        size_t urlStart = urlPos + 24;  // length of "browser_download_url":"
                        size_t urlEnd   = assetObj.find("\"", urlStart);
                        if (urlEnd != std::string::npos) {
                            firmwareUrl = assetObj.substr(urlStart, urlEnd - urlStart);
                            log_info("AutoUpdate: Found firmware URL: " << firmwareUrl);
                        } else {
                            log_error("AutoUpdate: Could not find end quote for firmware URL");
                        }
                    } else {
                        log_error("AutoUpdate: Could not find browser_download_url for firmware");
                        // Debug: log part of the asset object to see what's there
                        std::string debugObj = assetObj.length() > 200 ? assetObj.substr(0, 200) + "..." : assetObj;
                        log_info("AutoUpdate: Asset object preview: " << debugObj);
                    }
                } else {
                    log_error("AutoUpdate: Could not find matching closing brace for firmware asset object");
                }
            } else {
                log_error("AutoUpdate: Could not find start of firmware asset object");
            }
        } else {
            log_error("AutoUpdate: firmware.bin not found in assets");
        }

        // Find index.html.gz URL - look for exact filename match
        size_t webUIPos = jsonResponse.find("\"name\":\"index.html.gz\"");
        if (webUIPos != std::string::npos) {
            log_info("AutoUpdate: Searching for index.html.gz download URL...");
            // Search for the asset object containing this name
            size_t assetStart = jsonResponse.rfind("{", webUIPos);
            if (assetStart != std::string::npos) {
                // Find the matching closing brace by counting braces
                size_t assetEnd = assetStart + 1;
                int braceCount = 1;
                while (assetEnd < jsonResponse.length() && braceCount > 0) {
                    if (jsonResponse[assetEnd] == '{') {
                        braceCount++;
                    } else if (jsonResponse[assetEnd] == '}') {
                        braceCount--;
                    }
                    if (braceCount > 0) assetEnd++;
                }
                
                if (braceCount == 0) {
                    std::string assetObj = jsonResponse.substr(assetStart, assetEnd - assetStart + 1);
                    log_info("AutoUpdate: Extracted asset object for index.html.gz");
                    size_t urlPos = assetObj.find("\"browser_download_url\":\"");
                    if (urlPos != std::string::npos) {
                        size_t urlStart = urlPos + 24;  // length of "browser_download_url":"
                        size_t urlEnd   = assetObj.find("\"", urlStart);
                        if (urlEnd != std::string::npos) {
                            webUIUrl = assetObj.substr(urlStart, urlEnd - urlStart);
                            log_info("AutoUpdate: Found WebUI URL: " << webUIUrl);
                        } else {
                            log_error("AutoUpdate: Could not find end quote for WebUI URL");
                        }
                    } else {
                        log_error("AutoUpdate: Could not find browser_download_url for WebUI");
                        // Debug: log part of the asset object to see what's there
                        std::string debugObj = assetObj.length() > 200 ? assetObj.substr(0, 200) + "..." : assetObj;
                        log_info("AutoUpdate: Asset object preview: " << debugObj);
                    }
                } else {
                    log_error("AutoUpdate: Could not find matching closing brace for WebUI asset object");
                }
            } else {
                log_error("AutoUpdate: Could not find start of WebUI asset object");
            }
        } else {
            log_error("AutoUpdate: index.html.gz not found in assets");
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
        size_t      hostStart = url.find("://") + 3;
        size_t      pathStart = url.find("/", hostStart);
        std::string host      = url.substr(hostStart, pathStart - hostStart);
        std::string path      = url.substr(pathStart);

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
        size_t  totalBytes = 0;

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

    bool AutoUpdate::downloadAndInstallFirmware(const std::string& firmwareUrl) {
        WiFiClientSecure client;
        client.setInsecure();

        // Parse URL to get host and path
        size_t hostStart = firmwareUrl.find("://") + 3;
        size_t pathStart = firmwareUrl.find("/", hostStart);
        std::string host = firmwareUrl.substr(hostStart, pathStart - hostStart);
        std::string path = firmwareUrl.substr(pathStart);

        if (!client.connect(host.c_str(), 443)) {
            log_error("AutoUpdate: Failed to connect for firmware download: " << host);
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

        // Wait for response and get content length
        bool headersPassed = false;
        size_t contentLength = 0;
        while (client.connected() && !headersPassed) {
            String line = client.readStringUntil('\n');
            if (line.startsWith("Content-Length: ")) {
                contentLength = line.substring(16).toInt();
            }
            if (line.length() <= 2) {
                headersPassed = true;
            }
        }

        if (!headersPassed) {
            log_error("AutoUpdate: Failed to read firmware download headers");
            client.stop();
            return false;
        }

        if (contentLength == 0) {
            log_error("AutoUpdate: No content length found in firmware download");
            client.stop();
            return false;
        }

        if (contentLength < 100000) { // Firmware should be at least 100KB
            log_error("AutoUpdate: Firmware file too small (" << contentLength << " bytes)");
            client.stop();
            return false;
        }

        log_info("AutoUpdate: Installing firmware directly (" << contentLength << " bytes)...");

        if (!Update.begin(contentLength)) {
            log_error("AutoUpdate: Failed to begin firmware update");
            client.stop();
            return false;
        }

        uint8_t buffer[1024];
        size_t bytesWritten = 0;

        while (client.connected() || client.available()) {
            if (client.available()) {
                size_t bytesRead = client.readBytes(buffer, sizeof(buffer));
                if (bytesRead > 0) {
                    if (Update.write(buffer, bytesRead) != bytesRead) {
                        log_error("AutoUpdate: Failed to write firmware data");
                        Update.abort();
                        client.stop();
                        return false;
                    }
                    bytesWritten += bytesRead;
                }
            }
            delay(1);
        }

        client.stop();

        if (bytesWritten != contentLength) {
            log_error("AutoUpdate: Incomplete firmware download (" << bytesWritten << "/" << contentLength << " bytes)");
            Update.abort();
            return false;
        }

        if (!Update.end(true)) {
            log_error("AutoUpdate: Failed to complete firmware update");
            return false;
        }

        log_info("AutoUpdate: Firmware update completed successfully (" << bytesWritten << " bytes)");
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
        size_t  bytesRead;
        size_t  totalBytes = 0;

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

        // Download WebUI to temporary location (small file, should fit in flash)
        std::string tempWebUI = "/tmp/index.html.gz";

        log_info("AutoUpdate: Downloading WebUI from: " << webUIUrl);
        if (!downloadFile(webUIUrl, tempWebUI)) {
            log_error("AutoUpdate: Failed to download WebUI");
            return false;
        }

        // Install WebUI first (less risky)
        if (!installWebUI(tempWebUI)) {
            log_error("AutoUpdate: Failed to install WebUI");
            remove(tempWebUI.c_str());
            return false;
        }

        // Clean up WebUI temporary file
        remove(tempWebUI.c_str());

        // Download and install firmware directly (no temporary file)
        log_info("AutoUpdate: Downloading and installing firmware from: " << firmwareUrl);
        if (!downloadAndInstallFirmware(firmwareUrl)) {
            log_error("AutoUpdate: Failed to download and install firmware");
            return false;
        }

        log_info("AutoUpdate: Update completed successfully. Restarting in 3 seconds...");
        delay(3000);
        ESP.restart();

        return true;
    }
}

#endif