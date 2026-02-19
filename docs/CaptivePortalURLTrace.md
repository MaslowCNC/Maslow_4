# Captive Portal URL Response Verification

## Instructions for Regenerating This Documentation

This section contains instructions for AI agents or developers to regenerate this documentation to match the current code implementation.

### Files to Analyze

1. **Main Implementation**: `firmware/FluidNC/src/WebUI/WebServer.cpp`
   - Locate the `Web_Server::begin()` function
   - Find the section: `if (WiFi.getMode() == WIFI_AP)`
   - Extract all `_webserver->on()` route registrations within this block
   - Note any Host header checks in `handle_root()`

2. **Handler Declarations**: `firmware/FluidNC/src/WebUI/WebServer.h`
   - Find all captive portal handler function declarations (search for "Captive portal detection handlers" comment)
   - List: `handle_generate_204`, `handle_hotspot_detect`, `handle_connecttest`, `handle_ncsi`, `handle_firefox_detect`, `handle_success`, `handle_nm_check`, `handle_kde_ok`, `handle_ubuntu_connectivity`

3. **Handler Implementations**: `firmware/FluidNC/src/WebUI/WebServer.cpp`
   - Locate each handler function implementation
   - Extract the exact `_webserver->send()` call including:
     - HTTP status code
     - Content type
     - Response body

### Regeneration Steps

1. **Create URL inventory**:
   - For each route registered with `_webserver->on()` in AP mode
   - Note: path, HTTP method, handler function name
   - Include inline documentation comments above each route

2. **Document Host header checks**:
   - Scan `handle_root()` for `_webserver->hostHeader()` comparisons
   - Document which domains trigger special handling
   - Note which handler gets called for each domain

3. **Verify each URL**:
   - For each path or Host header pattern:
     - Full URL (reconstruct from inline comments)
     - Path or Host header pattern
     - Handler function called
     - HTTP response code and content-type
     - Response body content
     - Which platforms/OS/browsers use this URL
   
4. **Trace response flow**:
   - If handler is `handle_root()`, determine if it has Host header checks
   - If URL not explicitly registered, trace through `handle_not_found()`
   - Document that in AP mode, `handle_not_found()` calls `sendCaptivePortal()` which returns HTTP 200

5. **Group by platform**:
   - Android/Chrome/Brave (HTTP 204 responses)
   - iOS/macOS (HTTP 200 with HTML "Success")
   - Windows (HTTP 200 with specific text)
   - Firefox (HTTP 200 with HTML or "success" text)
   - Linux (GNOME, KDE, Ubuntu)
   - Other devices (Kindle, legacy)

6. **Verify correctness**:
   - Each URL must return the response expected by its platform
   - HTTP 204 = no captive portal for Android/Chrome
   - HTTP 200 with "Success" = no captive portal for Apple
   - HTTP 200 with specific text = no captive portal for Windows/Linux

### Expected Output Format

For each URL, include:
```markdown
#### N. `http://full-url-here/path`
- **Path**: `/path` or **Host Header**: `domain.com`
- **Handler**: `handler_function_name()`
- **Response**: HTTP STATUS with description
- **Code**: `_webserver->send(code, "type", "body");`
- **Status**: ✅ CORRECT
```

### Validation

After regeneration, verify:
- All routes in `Web_Server::begin()` under `WIFI_AP` are documented
- All Host header checks in `handle_root()` are documented
- Response codes match platform expectations
- Handler function code snippets are accurate
- Total URL count matches implementation (currently 20 patterns: 15 path routes + 5 Host header checks)

---

## Overview

When devices connect to the Maslow's WiFi Access Point, they automatically check specific URLs to determine if internet access is available. If these URLs don't respond correctly, the device treats the network as a "captive portal" (like hotel/airport WiFi requiring login), which triggers:
- Limited browser functionality
- Automatic disconnection after timeout
- Poor user experience

By responding to these URLs with the expected content, we "trick" devices into thinking they have full internet access, allowing normal web browsing of the Maslow's interface.

## URL Tracing and Verification

### Android / Chrome / Chromium / Brave

#### 1. `http://connectivitycheck.gstatic.com/generate_204`
- **Path**: `/generate_204`
- **Handler**: `handle_generate_204()`
- **Response**: HTTP 204 No Content with Google-compatible headers
- **Code**: 
  ```cpp
  _webserver->sendHeader("Content-Length", "0");
  _webserver->sendHeader("Cross-Origin-Resource-Policy", "cross-origin");
  _webserver->send(204, "text/plain", "");
  ```
- **Status**: ✅ CORRECT

#### 2. `http://clients3.google.com/gen_204`
- **Path**: `/gen_204`
- **Handler**: `handle_generate_204()`
- **Response**: HTTP 204 No Content with Google-compatible headers
- **Code**: 
  ```cpp
  _webserver->sendHeader("Content-Length", "0");
  _webserver->sendHeader("Cross-Origin-Resource-Policy", "cross-origin");
  _webserver->send(204, "text/plain", "");
  ```
- **Status**: ✅ CORRECT

#### 3. `http://clients3.google.com/generate_204`
- **Path**: `/generate_204`
- **Handler**: `handle_generate_204()`
- **Response**: HTTP 204 No Content with Google-compatible headers
- **Code**: 
  ```cpp
  _webserver->sendHeader("Content-Length", "0");
  _webserver->sendHeader("Cross-Origin-Resource-Policy", "cross-origin");
  _webserver->send(204, "text/plain", "");
  ```
- **Status**: ✅ CORRECT

#### 3a. Google/Android Host Header Detection (root path)
- **URLs**: `http://connectivitycheck.gstatic.com/`, `http://clients3.google.com/`, or any path with Google domain
- **Detection**: Host header contains `gstatic.com`, `google.com`, or `connectivitycheck`
- **Handler**: `handle_root()` → checks Host header → `handle_generate_204()`
- **Response**: HTTP 204 No Content with Google-compatible headers
- **Code**: Same as above
- **Note**: Handles cases where Android/Chrome requests root path with Google Host header
- **Status**: ✅ CORRECT

### iOS / macOS (Apple)

#### 4. `http://captive.apple.com/hotspot-detect.html`
- **Path**: `/hotspot-detect.html`
- **Handler**: `handle_hotspot_detect()`
- **Response**: HTTP 200 OK with HTML containing "Success"
- **Code**: `_webserver->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");`
- **Status**: ✅ CORRECT

#### 5. `http://www.apple.com/library/test/success.html`
- **Path**: `/library/test/success.html`
- **Handler**: `handle_hotspot_detect()`
- **Response**: HTTP 200 OK with HTML containing "Success"
- **Code**: `_webserver->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");`
- **Status**: ✅ CORRECT

#### 6. Legacy Apple URLs (iOS 6.x and earlier)
- **URL**: `http://www.appleiphonecell.com/`
- **Detection**: Host header equals `www.appleiphonecell.com`
- **Handler**: `handle_root()` → checks Host header → `handle_hotspot_detect()`
- **Response**: HTTP 200 OK with HTML containing "Success"
- **Code**: `_webserver->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");`
- **Note**: Legacy iOS devices use this URL for captive portal detection
- **Status**: ✅ CORRECT

Other legacy URLs (`http://www.apple.com/`, `http://captive.apple.com/`, `http://www.itools.info/`, `http://www.ibook.info/`, `http://www.airport.us/`, `http://www.thinkdifferent.us/`) are redirected by DNS to ESP32 IP. When paths not found, `handle_not_found()` calls `sendCaptivePortal()` in AP mode which returns HTTP 200 with HTML.

### Windows (Microsoft)

#### 7. `http://www.msftconnecttest.com/connecttest.txt`
- **Path**: `/connecttest.txt`
- **Handler**: `handle_connecttest()`
- **Response**: HTTP 200 OK with text "Microsoft Connect Test"
- **Code**: `_webserver->send(200, "text/plain", "Microsoft Connect Test");`
- **Status**: ✅ CORRECT

#### 8. `http://www.msftncsi.com/ncsi.txt`
- **Path**: `/ncsi.txt`
- **Handler**: `handle_ncsi()`
- **Response**: HTTP 200 OK with text "Microsoft NCSI"
- **Code**: `_webserver->send(200, "text/plain", "Microsoft NCSI");`
- **Status**: ✅ CORRECT

#### 9. `http://edge-http.microsoft.com/captiveportal/generate_204`
- **Path**: `/generate_204`
- **Handler**: `handle_generate_204()`
- **Response**: HTTP 204 No Content with Google-compatible headers
- **Code**: 
  ```cpp
  _webserver->sendHeader("Content-Length", "0");
  _webserver->sendHeader("Cross-Origin-Resource-Policy", "cross-origin");
  _webserver->send(204, "text/plain", "");
  ```
- **Status**: ✅ CORRECT

#### 10. `http://www.msftconnecttest.com/fwlink/`
- **Path**: `/fwlink/`
- **Handler**: `handle_root()`
- **Response**: HTTP 200 OK with HTML (index.html or PAGE_NOFILES)
- **Status**: ✅ CORRECT (redirect URL, 200 with HTML acceptable)

#### 11. `http://www.msftconnecttest.com/fwlink`
- **Path**: `/fwlink`
- **Handler**: `handle_root()`
- **Response**: HTTP 200 OK with HTML (index.html or PAGE_NOFILES)
- **Status**: ✅ CORRECT (redirect URL, 200 with HTML acceptable)

#### 12. `http://www.msftconnecttest.com/redirect`
- **Path**: `/redirect`
- **Handler**: `handle_root()`
- **Response**: HTTP 200 OK with HTML (index.html or PAGE_NOFILES)
- **Status**: ✅ CORRECT (redirect URL, 200 with HTML acceptable)

### Firefox (Mozilla)

#### 13. `http://detectportal.firefox.com/canonical.html`
- **Path**: `/canonical.html`
- **Handler**: `handle_firefox_detect()`
- **Response**: HTTP 200 OK with exact meta tag (no whitespace/newlines, matches Mozilla's canonical server)
- **Code**: `_webserver->send(200, "text/html", "<meta http-equiv=\"refresh\" content=\"0;url=https://support.mozilla.org/kb/captive-portal\"/>");`
- **Note**: Firefox requires EXACT match - any deviation triggers captive portal detection
- **Status**: ✅ CORRECT

#### 14. `http://detectportal.firefox.com/success.txt`
- **Path**: `/success.txt`
- **Handler**: `handle_success()`
- **Response**: HTTP 200 OK with text "success"
- **Code**: `_webserver->send(200, "text/plain", "success");`
- **Status**: ✅ CORRECT

### Linux (GNOME NetworkManager)

#### 15. `http://nmcheck.gnome.org/check_network_status.txt`
- **Path**: `/check_network_status.txt`
- **Handler**: `handle_nm_check()`
- **Response**: HTTP 200 OK with text "NetworkManager is online"
- **Code**: `_webserver->send(200, "text/plain", "NetworkManager is online");`
- **Status**: ✅ CORRECT

### Linux (KDE Plasma)

#### 16. `http://networkcheck.kde.org/` (any path)
- **Detection**: Host header equals `networkcheck.kde.org`
- **Handler**: `handle_root()` → checks Host header → `handle_kde_ok()`
- **Response**: HTTP 200 OK with text "OK"
- **Code**: `_webserver->send(200, "text/plain", "OK");`
- **Status**: ✅ CORRECT

### Linux (Ubuntu)

#### 17. `http://connectivity-check.ubuntu.com/` (any path)
- **Detection**: Host header equals `connectivity-check.ubuntu.com` or `connectivity-check.ubuntu.com.`
- **Handler**: `handle_root()` → checks Host header → `handle_ubuntu_connectivity()`
- **Response**: HTTP 204 No Content with `x-networkmanager-status: online` header
- **Code**: 
  ```cpp
  _webserver->sendHeader("x-networkmanager-status", "online");
  _webserver->sendHeader("Content-Length", "0");
  _webserver->send(204, "text/plain", "");
  ```
- **Note**: NetworkManager specifically checks for x-networkmanager-status header to determine online status
- **Status**: ✅ CORRECT

### Amazon Kindle and Fire Devices

#### 18. `http://spectrum.s3.amazonaws.com/kindle-wifi/wifistub.html`
- **Path**: `/kindle-wifi/wifistub.html`
- **Handler**: `handle_hotspot_detect()`
- **Response**: HTTP 200 OK with HTML containing "Success"
- **Code**: `_webserver->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");`
- **Note**: Kindle accepts the same simple "Success" HTML structure as Apple devices
- **Status**: ✅ CORRECT

### Legacy and Other Devices

#### 19. `http://*/mobile/status.php` (various hosts)
- **Path**: `/mobile/status.php`
- **Handler**: `handle_success()`
- **Response**: HTTP 200 OK with text "success"
- **Code**: `_webserver->send(200, "text/plain", "success");`
- **Status**: ✅ CORRECT

## Response Type Summary

### HTTP 204 No Content
Used by Android, Chrome, Chromium, Brave, Windows 10 (alternative), and Ubuntu.

**Handler for Google/Android/Chrome/Brave**: `handle_generate_204()`
- `/generate_204`
- `/gen_204`
- Host headers: `gstatic.com`, `google.com`, `connectivitycheck`
- Headers sent: `Content-Length: 0`, `Cross-Origin-Resource-Policy: cross-origin`

**Handler for Ubuntu**: `handle_ubuntu_connectivity()`
- Host: `connectivity-check.ubuntu.com` or `connectivity-check.ubuntu.com.`
- Headers sent: `x-networkmanager-status: online`, `Content-Length: 0`

### HTTP 200 with HTML "Success"
Used by iOS, macOS, and Kindle devices.

**Handler**: `handle_hotspot_detect()`
- `/hotspot-detect.html`
- `/library/test/success.html`
- `/kindle-wifi/wifistub.html`

### HTTP 200 with Platform-Specific Text

**Windows (current)**: `handle_connecttest()`
- `/connecttest.txt` → "Microsoft Connect Test"

**Windows (legacy)**: `handle_ncsi()`
- `/ncsi.txt` → "Microsoft NCSI"

**Firefox**: `handle_success()`
- `/success.txt` → "success"
- `/mobile/status.php` → "success"

**Linux GNOME**: `handle_nm_check()`
- `/check_network_status.txt` → "NetworkManager is online"

**Linux KDE**: `handle_kde_ok()`
- Host: `networkcheck.kde.org` → "OK"

### HTTP 200 with HTML Pages
Used for Microsoft redirect URLs and legacy Apple URLs.

**Handler**: `handle_root()` or `sendCaptivePortal()`
- `/fwlink`, `/fwlink/`, `/redirect`
- Legacy Apple URLs (via DNS redirect)

## DNS Redirection

The Maslow's DNS server is configured with wildcard redirection (`"*"`), meaning:
1. All domain names resolve to the ESP32's IP address
2. Clients request URLs like `http://captive.apple.com/hotspot-detect.html`
3. DNS resolves `captive.apple.com` to the ESP32's IP
4. WebServer receives request with path `/hotspot-detect.html` and Host header `captive.apple.com`
5. Path-based routing handles the request

## Implementation Details

### File Locations
- **Implementation**: `firmware/FluidNC/src/WebUI/WebServer.cpp`
- **Declarations**: `firmware/FluidNC/src/WebUI/WebServer.h`

### Route Registration
All routes are registered in `Web_Server::begin()` when `WiFi.getMode() == WIFI_AP`.

### Handler Functions
Each handler is a static member function of the `Web_Server` class that:
1. Receives the HTTP request via `_webserver`
2. Sends the appropriate response using `_webserver->send()`
3. Returns control to the WebServer

### Host Header Detection
Special handling in `handle_root()` checks the HTTP Host header for:
- KDE Plasma: `networkcheck.kde.org`
- Ubuntu: `connectivity-check.ubuntu.com` or `connectivity-check.ubuntu.com.`

## Verification Result

✅ **ALL CAPTIVE PORTAL DETECTION URLs RETURN CORRECT RESPONSES**

All 20 documented captive portal detection URL patterns (15 path-based routes + 5 Host header checks) have been verified to return the appropriate HTTP response codes and content that indicate the network has full internet access (even though it's a local AP without internet).

This prevents devices from:
- Showing captive portal login prompts
- Opening limited browsers
- Disconnecting after timeout
- Restricting network functionality

### Handler Summary

**9 Dedicated Handler Functions:**
1. `handle_generate_204()` - HTTP 204 with Google-compatible headers (Android/Chrome/Brave)
2. `handle_hotspot_detect()` - HTTP 200 with HTML "Success" (iOS/macOS/Kindle)
3. `handle_connecttest()` - HTTP 200 with "Microsoft Connect Test" (Windows)
4. `handle_ncsi()` - HTTP 200 with "Microsoft NCSI" (Windows legacy)
5. `handle_firefox_detect()` - HTTP 200 with exact meta tag (Firefox)
6. `handle_success()` - HTTP 200 with "success" text (Firefox/legacy)
7. `handle_nm_check()` - HTTP 200 with "NetworkManager is online" (GNOME)
8. `handle_kde_ok()` - HTTP 200 with "OK" (KDE Plasma)
9. `handle_ubuntu_connectivity()` - HTTP 204 with x-networkmanager-status header (Ubuntu)

**15 Path-Based Routes** registered in `Web_Server::begin()` under AP mode
**5 Host Header Checks** in `handle_root()` for special domain handling
