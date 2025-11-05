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

    static const size_t DOWNLOAD_BUFFER_SIZE = 1024;

    std::string AutoUpdate::getLatestReleaseInfo() {
        WiFiClientSecure client;
        client.setInsecure();  // For simplicity, disable certificate verification

        // Get the configurable update URL
        std::string updateURL = config->_maslowUpdateURL;
        
        // Send HTTP request, parse headers, and handle redirects automatically
        HttpResponse httpResp = sendHttpRequestAndParseHeaders(&client, updateURL, "Accept: application/vnd.github.v3+json\r\n", "API");
        
        if (!httpResp.headersParsed) {
            return "";
        }
        
        if (httpResp.httpStatus != 200) {
            log_error("AutoUpdate: API HTTP error status: " << httpResp.httpStatus);
            client.stop();
            return "";
        }

        // Read response body
        std::string response;
        while (client.available()) {
            String line = client.readStringUntil('\n');
            response += line.c_str();
        }

        client.stop();
        return response;
    }

    // Helper function to parse version string and extract major.minor.patch components
    // Handles formats like "v1.12", "v1.12.3", "v1.12-2-abcdef"
    struct Version {
        int major = 0;
        int minor = 0; 
        int patch = 0;
        bool hasCommits = false; // true if version has commits after tag (e.g., v1.12-2-abcdef)
        
        Version(const std::string& versionStr) {
            std::string v = versionStr;
            // Remove 'v' prefix if present
            if (!v.empty() && v[0] == 'v') {
                v = v.substr(1);
            }
            
            // Check if version has additional commits after a dash (e.g., v1.12-2)
            size_t dashPos = v.find('-');
            if (dashPos != std::string::npos) {
                // Extract just the version part before the dash
                hasCommits = true;
                v = v.substr(0, dashPos);
            }
            
            // Parse major.minor.patch
            std::vector<std::string> parts;
            size_t start = 0;
            size_t pos = 0;
            while ((pos = v.find('.', start)) != std::string::npos) {
                parts.push_back(v.substr(start, pos - start));
                start = pos + 1;
            }
            parts.push_back(v.substr(start)); // Add the last part
            
            if (parts.size() >= 1) major = std::stoi(parts[0]);
            if (parts.size() >= 2) minor = std::stoi(parts[1]);  
            if (parts.size() >= 3) patch = std::stoi(parts[2]);
        }
        
        // Compare versions: return true if this version is newer than other
        bool isNewerThan(const Version& other) const {
            if (major != other.major) return major > other.major;
            if (minor != other.minor) return minor > other.minor;
            if (patch != other.patch) return patch > other.patch;
            
            // If base versions are equal, check if one has additional commits
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
    HttpResponse AutoUpdate::sendHttpRequestAndParseHeaders(WiFiClientSecure* client, const std::string& url, const std::string& extraHeaders, const std::string& logPrefix, int maxRedirects) {
        HttpResponse response;
        response.finalUrl = url;
        
        // Handle redirects automatically up to maxRedirects times
        for (int redirectCount = 0; redirectCount <= maxRedirects; redirectCount++) {
            // Parse URL to get host and path
            size_t hostStart = response.finalUrl.find("://") + 3;
            size_t pathStart = response.finalUrl.find("/", hostStart);
            std::string host = response.finalUrl.substr(hostStart, pathStart - hostStart);
            std::string path = response.finalUrl.substr(pathStart);

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
            response.httpStatus = 0;
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
                            log_info("AutoUpdate: " << logPrefix << " HTTP Status: " << response.httpStatus);
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
                response.finalUrl = response.redirectLocation;
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

    // Helper function to extract download URL for a specific asset from JSON
    std::string AutoUpdate::extractAssetDownloadURL(const std::string& jsonResponse, const std::string& assetName) {
        std::string searchPattern = "\"name\":\"" + assetName + "\"";
        size_t assetPos = jsonResponse.find(searchPattern);
        if (assetPos == std::string::npos) {
            log_error("AutoUpdate: " << assetName << " not found in assets");
            return "";
        }

        log_info("AutoUpdate: Searching for " << assetName << " download URL...");
        
        // Search for the asset object containing this name
        size_t assetStart = jsonResponse.rfind("{", assetPos);
        if (assetStart == std::string::npos) {
            log_error("AutoUpdate: Could not find start of " << assetName << " asset object");
            return "";
        }

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
        
        if (braceCount != 0) {
            log_error("AutoUpdate: Could not find matching closing brace for " << assetName << " asset object");
            return "";
        }

        std::string assetObj = jsonResponse.substr(assetStart, assetEnd - assetStart + 1);
        log_info("AutoUpdate: Extracted asset object for " << assetName);
        
        size_t urlPos = assetObj.find("\"browser_download_url\":\"");
        if (urlPos == std::string::npos) {
            log_error("AutoUpdate: Could not find browser_download_url for " << assetName);
            // Debug: log part of the asset object to see what's there
            std::string debugObj = assetObj.length() > 200 ? assetObj.substr(0, 200) + "..." : assetObj;
            log_info("AutoUpdate: Asset object preview: " << debugObj);
            return "";
        }
        
        size_t urlStart = urlPos + 24;  // length of "browser_download_url":"
        size_t urlEnd   = assetObj.find("\"", urlStart);
        if (urlEnd == std::string::npos) {
            log_error("AutoUpdate: Could not find end quote for " << assetName << " URL");
            return "";
        }
        
        std::string downloadUrl = assetObj.substr(urlStart, urlEnd - urlStart);
        log_info("AutoUpdate: Found " << assetName << " URL: " << downloadUrl);
        return downloadUrl;
    }

    // Helper function to download data from client to file with common logic
    bool AutoUpdate::downloadToFile(WiFiClientSecure& client, FILE* file, size_t expectedSize, const std::string& logPrefix, size_t* actualSize) {
        uint8_t buffer[DOWNLOAD_BUFFER_SIZE];
        size_t totalBytes = 0;

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
        // Use grbl_version as the current version (e.g., "3.0")
        std::string currentVersion = grbl_version;
        // Add "v" prefix if not already present
        if (currentVersion.empty() || currentVersion[0] != 'v') {
            currentVersion = std::string("v") + currentVersion;
        }
        
        // Compare versions using semantic versioning logic
        if (tagName.empty() || !isNewerVersion(tagName, currentVersion)) {
            log_info("AutoUpdate: Already running the latest version or newer (" << currentVersion << ")");
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

        // Extract download URLs for required assets
        firmwareUrl = extractAssetDownloadURL(jsonResponse, "firmware.bin");
        webUIUrl = extractAssetDownloadURL(jsonResponse, "index.html.gz");

        if (firmwareUrl.empty() || webUIUrl.empty()) {
            log_error("AutoUpdate: Required files not found in release");
            return false;
        }

        log_info("AutoUpdate: New version available. Starting download...");
        return downloadAndInstallUpdate(firmwareUrl, webUIUrl);
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
        FluidPath tempPath(tempFilename, localfsName, ec);
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
        bool downloadSuccess = downloadToFile(client, file, httpResp.contentLength > 0 ? httpResp.contentLength : 0, "WebUI download", &totalBytes);
        
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

        if (httpResp.contentLength < 100000) { // Firmware should be at least 100KB
            log_error("AutoUpdate: Firmware file too small (" << httpResp.contentLength << " bytes)");
            client.stop();
            return false;
        }

        log_info("AutoUpdate: Downloading firmware to SD card (" << httpResp.contentLength << " bytes)...");

        // Download firmware to SD card first
        std::string firmwareFilename = "firmware.bin.tmp";
        std::error_code ec;
        FluidPath firmwareTempPath(firmwareFilename, sdName, ec);
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
        bool downloadSuccess = downloadToFile(client, file, httpResp.contentLength, "firmware download", &bytesDownloaded);
        
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
        size_t bytesInstalled = 0;
        while (bytesInstalled < bytesDownloaded) {
            size_t bytesRead = fread(buffer, 1, sizeof(buffer), firmwareFile);
            if (bytesRead == 0) {
                break; // End of file or error
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