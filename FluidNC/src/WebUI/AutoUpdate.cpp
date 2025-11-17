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
#    include "Driver/localfs.h"
#    include "../FluidPath.h"

#    include <WiFiClientSecure.h>
#    include <Update.h>
#    include <vector>
#    include <string>

namespace WebUI {

    static const size_t DOWNLOAD_BUFFER_SIZE  = 1024;
    static const size_t MAX_API_RESPONSE_SIZE = 100000;  // Maximum expected size for GitHub API response

    // Helper function to extract a quoted string value after a key in a JSON buffer
    // Returns true if found, false otherwise
    // startPos: position to start searching from
    static bool extractJsonString(const std::string& buffer, const std::string& key, std::string& value, size_t startPos = 0) {
        std::string searchPattern = "\"" + key + "\":\"";
        size_t      pos           = buffer.find(searchPattern, startPos);
        if (pos == std::string::npos) {
            return false;
        }

        size_t valueStart = pos + searchPattern.length();
        size_t valueEnd   = buffer.find("\"", valueStart);
        if (valueEnd == std::string::npos) {
            return false;
        }

        // Simple handling of escaped quotes - look for next unescaped quote
        while (valueEnd > valueStart && buffer[valueEnd - 1] == '\\') {
            valueEnd = buffer.find("\"", valueEnd + 1);
            if (valueEnd == std::string::npos) {
                return false;
            }
        }

        value = buffer.substr(valueStart, valueEnd - valueStart);
        return true;
    }

    // Parse GitHub release API response using streaming to minimize memory usage
    // Only extracts the fields we need: tag_name and browser_download_url for specific assets
    bool AutoUpdate::parseReleaseInfoStreaming(WiFiClientSecure* client, ReleaseInfo& info) {
        // Use a sliding window buffer to search for JSON patterns
        // This avoids loading the entire ~18KB response into memory
        const size_t WINDOW_SIZE = 2048;  // Large enough to capture JSON fields
        std::string  window;
        window.reserve(WINDOW_SIZE);

        bool   foundTagName         = false;
        bool   foundFirmware        = false;
        bool   foundWebUI           = false;
        size_t firmwareAssetNamePos = std::string::npos;
        size_t webUIAssetNamePos    = std::string::npos;

        char buffer[512];

        while (client->connected() || client->available()) {
            if (client->available()) {
                size_t bytesRead = client->readBytes(buffer, sizeof(buffer));
                if (bytesRead > 0) {
                    size_t oldWindowSize = window.length();

                    // Append new data to window
                    window.append(buffer, bytesRead);

                    // Keep window size manageable by trimming old data
                    size_t trimAmount = 0;
                    if (window.length() > WINDOW_SIZE) {
                        trimAmount = window.length() - WINDOW_SIZE;
                        window.erase(0, trimAmount);

                        // Adjust position markers after trimming
                        if (firmwareAssetNamePos != std::string::npos) {
                            if (firmwareAssetNamePos < trimAmount) {
                                firmwareAssetNamePos = std::string::npos;
                            } else {
                                firmwareAssetNamePos -= trimAmount;
                            }
                        }
                        if (webUIAssetNamePos != std::string::npos) {
                            if (webUIAssetNamePos < trimAmount) {
                                webUIAssetNamePos = std::string::npos;
                            } else {
                                webUIAssetNamePos -= trimAmount;
                            }
                        }
                    }

                    // Look for tag_name if not found yet
                    if (!foundTagName && extractJsonString(window, "tag_name", info.tagName)) {
                        foundTagName = true;
                        log_debug("AutoUpdate: Found tag_name: " << info.tagName);
                    }

                    // Look for firmware.bin asset name
                    if (firmwareAssetNamePos == std::string::npos) {
                        firmwareAssetNamePos = window.find("\"name\":\"firmware.bin\"");
                        if (firmwareAssetNamePos != std::string::npos) {
                            log_debug("AutoUpdate: Found firmware.bin asset");
                        }
                    }
                    // If we found the asset name, look for its download URL after that position
                    if (firmwareAssetNamePos != std::string::npos && !foundFirmware) {
                        if (extractJsonString(window, "browser_download_url", info.firmwareUrl, firmwareAssetNamePos)) {
                            foundFirmware        = true;
                            firmwareAssetNamePos = std::string::npos;  // Reset for next search
                            log_debug("AutoUpdate: Found firmware.bin URL");
                        }
                    }

                    // Look for index.html.gz asset name
                    if (webUIAssetNamePos == std::string::npos) {
                        webUIAssetNamePos = window.find("\"name\":\"index.html.gz\"");
                        if (webUIAssetNamePos != std::string::npos) {
                            log_debug("AutoUpdate: Found index.html.gz asset");
                        }
                    }
                    // If we found the asset name, look for its download URL after that position
                    if (webUIAssetNamePos != std::string::npos && !foundWebUI) {
                        if (extractJsonString(window, "browser_download_url", info.webUIUrl, webUIAssetNamePos)) {
                            foundWebUI        = true;
                            webUIAssetNamePos = std::string::npos;  // Reset for next search
                            log_debug("AutoUpdate: Found index.html.gz URL");
                        }
                    }

                    // Early exit if we found everything
                    if (foundTagName && foundFirmware && foundWebUI) {
                        log_debug("AutoUpdate: Found all required information");
                        break;
                    }
                }
            } else {
                delay(1);
            }
        }

        return foundTagName && foundFirmware && foundWebUI;
    }

    // Helper function to parse version string and extract major.minor.patch components
    // Handles formats like "v1.12", "v1.12.3", "v1.12-2-abcdef"
    struct Version {
        int  major      = 0;
        int  minor      = 0;
        int  patch      = 0;
        bool hasCommits = false;  // true if version has commits after tag (e.g., v1.12-2-abcdef)

        Version(const std::string& versionStr) {
            std::string v = versionStr;
            // Remove 'v' prefix if present
            if (!v.empty() && v[0] == 'v') {
                v = v.substr(1);
            }

            // Check if this is a git-annotated version (has commits after tag)
            size_t dashPos = v.find('-');
            if (dashPos != std::string::npos) {
                // Extract just the version part before the dash
                hasCommits = true;
                v          = v.substr(0, dashPos);
            }

            // Parse major.minor.patch
            std::vector<std::string> parts;
            size_t                   start = 0;
            size_t                   pos   = 0;
            while ((pos = v.find('.', start)) != std::string::npos) {
                parts.push_back(v.substr(start, pos - start));
                start = pos + 1;
            }
            parts.push_back(v.substr(start));  // Add the last part

            if (parts.size() >= 1)
                major = std::stoi(parts[0]);
            if (parts.size() >= 2)
                minor = std::stoi(parts[1]);
            if (parts.size() >= 3)
                patch = std::stoi(parts[2]);
        }

        // Compare versions: return true if this version is newer than other
        bool isNewerThan(const Version& other) const {
            if (major != other.major)
                return major > other.major;
            if (minor != other.minor)
                return minor > other.minor;
            if (patch != other.patch)
                return patch > other.patch;

            // If base versions are equal, consider git-annotated versions as "newer"
            // This handles cases like v1.12 (release) vs v1.12-2-abcdef (dev build after release)
            return hasCommits && !other.hasCommits;
        }
    };

    // Check if latestVersion is newer than currentVersion
    bool AutoUpdate::isNewerVersion(const std::string& latestVersion, const std::string& currentVersion) {
        try {
            Version latest(latestVersion);
            Version current(currentVersion);
            return latest.isNewerThan(current);
        } catch (const std::exception& e) {
            // If version parsing fails, fall back to string comparison
            log_info("AutoUpdate: Version parsing failed, using string comparison");
            return latestVersion != currentVersion;
        }
    }

    // Consolidated HTTP request, header parsing, and redirect handling function
    HttpResponse AutoUpdate::sendHttpRequestAndParseHeaders(
        WiFiClientSecure* client, const std::string& url, const std::string& extraHeaders, const std::string& logPrefix, int maxRedirects) {
        HttpResponse response;
        response.finalUrl = url;

        // Handle redirects automatically up to maxRedirects times
        for (int redirectCount = 0; redirectCount <= maxRedirects; redirectCount++) {
            // Parse URL to get host and path
            size_t      hostStart = response.finalUrl.find("://") + 3;
            size_t      pathStart = response.finalUrl.find("/", hostStart);
            std::string host      = response.finalUrl.substr(hostStart, pathStart - hostStart);
            std::string path      = response.finalUrl.substr(pathStart);

            if (!client->connect(host.c_str(), 443)) {
                log_error("AutoUpdate: Failed to connect to " << host << " for " << logPrefix);
                response.headersParsed = false;
                return response;
            }

            // Send HTTP request
            client->print("GET ");
            client->print(path.c_str());
            client->print(" HTTP/1.1\r\n");
            client->print("Host: ");
            client->print(host.c_str());
            client->print("\r\n");
            client->print("User-Agent: FluidNC-AutoUpdate\r\n");
            if (!extraHeaders.empty()) {
                client->print(extraHeaders.c_str());
            }
            client->print("Connection: close\r\n\r\n");

            // Wait for response
            unsigned long timeout = millis() + 10000;  // 10 second timeout
            while (!client->available() && millis() < timeout) {
                delay(10);
            }

            if (!client->available()) {
                log_error("AutoUpdate: " << logPrefix << " request timeout");
                client->stop();
                response.headersParsed = false;
                return response;
            }

            // Reset response data for each attempt
            response.httpStatus    = 0;
            response.contentLength = -1;
            response.redirectLocation.clear();

            // Parse HTTP response headers
            bool headersPassed = false;

            while (client->connected() && !headersPassed) {
                if (client->available()) {
                    String line = client->readStringUntil('\n');
                    line.trim();

                    // Parse HTTP status line
                    if (line.startsWith("HTTP/")) {
                        int statusStart = line.indexOf(' ') + 1;
                        int statusEnd   = line.indexOf(' ', statusStart);
                        if (statusStart > 0 && statusEnd > statusStart) {
                            response.httpStatus = line.substring(statusStart, statusEnd).toInt();
                            log_debug("AutoUpdate: " << logPrefix << " HTTP Status: " << response.httpStatus);
                        }
                    }

                    // Parse Content-Length header
                    if (line.startsWith("Content-Length:")) {
                        response.contentLength = line.substring(15).toInt();
                        log_info("AutoUpdate: " << logPrefix << " Content-Length: " << response.contentLength);
                    }

                    // Parse Location header for redirects
                    if (line.startsWith("Location:")) {
                        response.redirectLocation = line.substring(9).c_str();
                        response.redirectLocation.erase(0, response.redirectLocation.find_first_not_of(" \t\r\n"));
                        log_info("AutoUpdate: " << logPrefix << " redirect location: " << response.redirectLocation);
                    }

                    if (line.length() == 0) {  // Empty line means end of headers
                        headersPassed = true;
                    }
                }
                delay(1);
            }

            if (!headersPassed) {
                log_error("AutoUpdate: Failed to read " << logPrefix << " response headers");
                client->stop();
                response.headersParsed = false;
                return response;
            }

            // Check if we need to follow a redirect
            if (response.httpStatus >= 301 && response.httpStatus <= 308 && !response.redirectLocation.empty()) {
                if (redirectCount >= maxRedirects) {
                    log_error("AutoUpdate: " << logPrefix << " too many redirects (" << redirectCount + 1 << ")");
                    client->stop();
                    response.headersParsed = false;
                    return response;
                }

                log_info("AutoUpdate: Following " << logPrefix << " redirect to: " << response.redirectLocation);
                client->stop();
                response.finalUrl      = response.redirectLocation;
                response.wasRedirected = true;

                // Continue the loop to follow the redirect
                continue;
            }

            // If we get here, no redirect needed - we have our final response
            response.headersParsed = true;
            return response;
        }

        // Should not reach here, but just in case
        response.headersParsed = false;
        return response;
    }

    // Helper function to download data from client to file with common logic
    bool AutoUpdate::downloadToFile(WiFiClientSecure& client, FILE* file, size_t expectedSize, const std::string& logPrefix, size_t* actualSize) {
        uint8_t buffer[DOWNLOAD_BUFFER_SIZE];
        size_t  totalBytes = 0;

        while (client.connected() || client.available()) {
            if (client.available()) {
                size_t bytesRead = client.readBytes(buffer, sizeof(buffer));
                if (bytesRead > 0) {
                    if (fwrite(buffer, 1, bytesRead, file) != bytesRead) {
                        log_error("AutoUpdate: Failed to write data for " << logPrefix);
                        return false;
                    }
                    totalBytes += bytesRead;
                }
            }
            delay(1);
        }

        if (actualSize) {
            *actualSize = totalBytes;
        }

        // Validate size if expected size was provided
        if (expectedSize > 0 && totalBytes != expectedSize) {
            log_error("AutoUpdate: " << logPrefix << " size mismatch. Expected: " << expectedSize << ", Got: " << totalBytes);
            return false;
        }

        if (totalBytes == 0) {
            log_error("AutoUpdate: No data downloaded for " << logPrefix);
            return false;
        }

        log_info("AutoUpdate: Successfully downloaded " << totalBytes << " bytes for " << logPrefix);
        return true;
    }

    bool AutoUpdate::checkForUpdate() {
        // Normalize the auto-update setting for backward compatibility
        // "true" -> "yes", "false" -> "no"
        std::string autoUpdateSetting = config->_maslowAutoUpdate;
        if (autoUpdateSetting == "true") {
            autoUpdateSetting = "yes";
        } else if (autoUpdateSetting == "false") {
            autoUpdateSetting = "no";
        }

        // Check if auto-update is set to "never" - if so, skip entirely
        if (autoUpdateSetting == "never") {
            log_info("AutoUpdate: check for new version disabled");
            return false;
        }

        // Only check if not in AP mode
        if (WiFi.getMode() == WIFI_AP) {
            log_debug("AutoUpdate: Skipping update check - in AP mode");
            return false;
        }

        log_debug("AutoUpdate: Checking for new release...");

        WiFiClientSecure client;
        client.setInsecure();  // For simplicity, disable certificate verification

        // Get the configurable update URL
        std::string updateURL = config->_maslowUpdateURL;

        // Send HTTP request, parse headers, and handle redirects automatically
        HttpResponse httpResp = sendHttpRequestAndParseHeaders(&client, updateURL, "Accept: application/vnd.github.v3+json\r\n", "API");

        if (!httpResp.headersParsed) {
            return false;
        }

        if (httpResp.httpStatus != 200) {
            log_error("AutoUpdate: API HTTP error status: " << httpResp.httpStatus);
            client.stop();
            return false;
        }

        // Parse the response using streaming to minimize memory usage
        ReleaseInfo releaseInfo;
        if (!parseReleaseInfoStreaming(&client, releaseInfo)) {
            log_error("AutoUpdate: Failed to parse release information");
            client.stop();
            return false;
        }

        client.stop();

        if (!releaseInfo.isValid()) {
            log_error("AutoUpdate: Incomplete release information");
            return false;
        }

        log_debug("AutoUpdate: Latest release: " << releaseInfo.tagName);

        // Check if this is a newer version than current
        // Extract just the version tag from git_info (e.g., "v1.12 (HEAD-08ab30d2)" -> "v1.12")
        std::string currentVersion = git_info;
        size_t      spacePos       = currentVersion.find(' ');
        if (spacePos != std::string::npos) {
            currentVersion = currentVersion.substr(0, spacePos);
        }

        // Compare versions using semantic versioning logic
        if (releaseInfo.tagName.empty() || !isNewerVersion(releaseInfo.tagName, currentVersion)) {
            log_info("AutoUpdate: Already running the latest version or newer (" << currentVersion << ")");
            return false;
        }

        log_info("AutoUpdate: Current version: " << currentVersion << ", Latest: " << releaseInfo.tagName);

        // Check auto-update setting to determine next action
        // "yes" or "true" = auto-download and install
        // "no" or "false" = notify but don't install
        // "never" = already handled earlier, won't reach here
        if (autoUpdateSetting != "yes") {
            log_info("AutoUpdate: New version available but auto-update is not set to 'yes' in configuration");
            return false;
        }

        log_info("AutoUpdate: New version available. Starting download...");
        return downloadAndInstallUpdate(releaseInfo.firmwareUrl, releaseInfo.webUIUrl);
    }

    bool AutoUpdate::downloadFileToLocalFS(const std::string& url, const std::string& filename) {
        // Check if local filesystem is available
        if (!localfsName) {
            log_error("AutoUpdate: Local filesystem not available");
            return false;
        }

        // Use temporary filename during download
        std::string tempFilename = filename + ".tmp";

        // Create FluidPath to handle proper filesystem mounting and path resolution for temp file
        std::error_code ec;
        FluidPath       tempPath(tempFilename, localfsName, ec);
        if (ec) {
            log_error("AutoUpdate: Failed to create temporary filesystem path: " << ec.message());
            return false;
        }

        WiFiClientSecure client;
        client.setInsecure();

        // Send HTTP request, parse headers, and handle redirects automatically
        HttpResponse httpResp = sendHttpRequestAndParseHeaders(&client, url, "", "WebUI download");

        if (!httpResp.headersParsed) {
            return false;
        }

        if (httpResp.httpStatus != 200) {
            log_error("AutoUpdate: HTTP error status: " << httpResp.httpStatus);
            client.stop();
            return false;
        }

        // Save to temporary file using proper filesystem path
        FILE* file = fopen(tempPath.c_str(), "wb");
        if (!file) {
            log_error("AutoUpdate: Failed to create temporary file: " << tempPath.c_str());
            client.stop();
            return false;
        }

        size_t totalBytes;
        bool   downloadSuccess =
            downloadToFile(client, file, httpResp.contentLength > 0 ? httpResp.contentLength : 0, "WebUI download", &totalBytes);

        fclose(file);
        client.stop();

        if (!downloadSuccess) {
            return false;
        }

        log_info("AutoUpdate: Downloaded " << totalBytes << " bytes to " << tempPath.c_str());

        // Move temporary file to final location
        FluidPath finalPath(filename, localfsName, ec);
        if (ec) {
            log_error("AutoUpdate: Failed to create final filesystem path: " << ec.message());
            return false;
        }

        if (rename(tempPath.c_str(), finalPath.c_str()) != 0) {
            log_error("AutoUpdate: Failed to rename temporary file to final location");
            return false;
        }

        log_info("AutoUpdate: Successfully moved file to " << finalPath.c_str());
        return true;
    }

    bool AutoUpdate::downloadAndInstallFirmware(const std::string& firmwareUrl) {
        WiFiClientSecure client;
        client.setInsecure();

        // Send HTTP request, parse headers, and handle redirects automatically
        HttpResponse httpResp = sendHttpRequestAndParseHeaders(&client, firmwareUrl, "", "Firmware");

        if (!httpResp.headersParsed) {
            return false;
        }

        if (httpResp.httpStatus != 200) {
            log_error("AutoUpdate: Firmware HTTP error status: " << httpResp.httpStatus);
            client.stop();
            return false;
        }

        if (httpResp.contentLength <= 0) {
            log_error("AutoUpdate: No content length found in firmware download");
            client.stop();
            return false;
        }

        if (httpResp.contentLength < 100000) {  // Firmware should be at least 100KB
            log_error("AutoUpdate: Firmware file too small (" << httpResp.contentLength << " bytes)");
            client.stop();
            return false;
        }

        log_info("AutoUpdate: Downloading firmware to SD card (" << httpResp.contentLength << " bytes)...");

        // Download firmware to SD card first
        std::string     firmwareFilename = "firmware.bin.tmp";
        std::error_code ec;
        FluidPath       firmwareTempPath(firmwareFilename, sdName, ec);
        if (ec) {
            log_error("AutoUpdate: Failed to create SD firmware path: " << ec.message());
            client.stop();
            return false;
        }

        // Save to temporary file on SD card
        FILE* file = fopen(firmwareTempPath.c_str(), "wb");
        if (!file) {
            log_error("AutoUpdate: Failed to create firmware file on SD card: " << firmwareTempPath.c_str());
            client.stop();
            return false;
        }

        size_t bytesDownloaded;
        bool   downloadSuccess = downloadToFile(client, file, httpResp.contentLength, "firmware download", &bytesDownloaded);

        fclose(file);
        client.stop();

        if (!downloadSuccess) {
            return false;
        }

        log_info("AutoUpdate: Firmware downloaded to SD card successfully (" << bytesDownloaded << " bytes)");

        // Now install firmware from the saved file
        log_info("AutoUpdate: Installing firmware from SD card...");

        // Move temporary file to final location
        FluidPath firmwareFinalPath("firmware.bin", sdName, ec);
        if (ec) {
            log_error("AutoUpdate: Failed to create final SD firmware path: " << ec.message());
            return false;
        }

        if (rename(firmwareTempPath.c_str(), firmwareFinalPath.c_str()) != 0) {
            log_error("AutoUpdate: Failed to rename firmware file to final location");
            return false;
        }

        // Open the firmware file for reading
        FILE* firmwareFile = fopen(firmwareFinalPath.c_str(), "rb");
        if (!firmwareFile) {
            log_error("AutoUpdate: Failed to open firmware file for installation: " << firmwareFinalPath.c_str());
            return false;
        }

        // Begin firmware update process
        if (!Update.begin(bytesDownloaded)) {
            log_error("AutoUpdate: Failed to begin firmware update");
            fclose(firmwareFile);
            return false;
        }

        // Install firmware from saved file
        uint8_t buffer[DOWNLOAD_BUFFER_SIZE];
        size_t  bytesInstalled = 0;
        while (bytesInstalled < bytesDownloaded) {
            size_t bytesRead = fread(buffer, 1, sizeof(buffer), firmwareFile);
            if (bytesRead == 0) {
                break;  // End of file or error
            }

            if (Update.write(buffer, bytesRead) != bytesRead) {
                log_error("AutoUpdate: Failed to write firmware data during installation");
                Update.abort();
                fclose(firmwareFile);
                return false;
            }
            bytesInstalled += bytesRead;
        }

        fclose(firmwareFile);

        if (bytesInstalled != bytesDownloaded) {
            log_error("AutoUpdate: Incomplete firmware installation (" << bytesInstalled << "/" << bytesDownloaded << " bytes)");
            Update.abort();
            return false;
        }

        if (!Update.end(true)) {
            log_error("AutoUpdate: Failed to complete firmware update");
            return false;
        }

        log_info("AutoUpdate: Firmware installation completed successfully (" << bytesInstalled << " bytes)");

        // Clean up firmware file after successful installation
        remove(firmwareFinalPath.c_str());
        log_info("AutoUpdate: Firmware file removed from SD card after successful installation");

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

        // Download WebUI directly to final destination using proper filesystem handling
        std::string webUIFilename = "index.html.gz";

        log_info("AutoUpdate: Downloading WebUI from: " << webUIUrl);
        if (!downloadFileToLocalFS(webUIUrl, webUIFilename)) {
            log_error("AutoUpdate: Failed to download WebUI");
            return false;
        }

        // WebUI is already in place, now download and install firmware directly
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