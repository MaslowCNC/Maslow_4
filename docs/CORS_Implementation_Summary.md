# CORS Implementation Summary

## Overview
This document summarizes the CORS (Cross-Origin Resource Sharing) implementation added to FluidNC firmware to enable direct G-code file uploads from https://abundance.maslowcnc.com/.

## Changes Made

### 1. Firmware Changes (firmware/FluidNC/src/WebUI/)

#### WebServer.h
- Added `sendCORSHeaders()` - Helper function to send CORS headers
- Added `handleCORSPreFlight()` - Handler for OPTIONS preflight requests

#### WebServer.cpp
- **sendCORSHeaders()**: Sends CORS headers allowing:
  - Origin: `https://abundance.maslowcnc.com`
  - Methods: GET, POST, OPTIONS
  - Headers: Content-Type, Authorization, Cookie
  - Credentials: Enabled (for authentication cookies)
  - Max Age: 86400 seconds (24 hours)

- **handleCORSPreFlight()**: Handles OPTIONS preflight requests with 204 No Content response

- **handleFileOps()**: Updated to:
  - Send CORS headers on all requests
  - Handle OPTIONS preflight requests before authentication check

- **sendJSON()**: Updated to:
  - Send CORS headers on all JSON responses
  - Use the `code` parameter instead of hardcoded 200

### 2. API Documentation (docs/API_FileUpload.md)

Created comprehensive documentation including:
- Endpoint descriptions for `/upload` (SD card) and `/files` (LocalFS)
- JavaScript code examples for file upload
- Complete `MaslowUploader` class implementation
- Authentication requirements
- Error handling guidance
- Troubleshooting tips

## How It Works

### CORS Flow
1. Browser makes a preflight OPTIONS request to check if cross-origin request is allowed
2. FluidNC responds with CORS headers indicating abundance.maslowcnc.com is allowed
3. Browser allows the actual POST request to proceed
4. FluidNC includes CORS headers in the response

### Upload Flow
1. External website creates FormData with file and size parameter
2. POST request sent to FluidNC endpoint (/upload or /files)
3. FluidNC authenticates the request (if auth is enabled)
4. File is uploaded and saved to SD card or LocalFS
5. JSON response returned with status and file listing

## Security Considerations

### What's Protected
- Authentication is still required (if enabled)
- Only abundance.maslowcnc.com can make cross-origin requests
- All existing security measures remain in place

### What's Not Changed
- No new attack vectors introduced
- Authentication mechanism unchanged
- File system permissions unchanged

## Testing Performed
- ✅ Firmware compiles successfully without errors
- ✅ Code formatted with clang-format
- ✅ No trailing whitespace
- ✅ Code review completed and feedback addressed
- ✅ RAM usage: 42.5% (139,244 / 327,680 bytes)
- ✅ Flash usage: 64.6% (1,988,649 / 3,080,192 bytes)

## Next Steps for abundance.maslowcnc.com Integration

1. **Implement the MaslowUploader class** from API_FileUpload.md
2. **Add IP address input** for users to specify their Maslow's IP
3. **Add upload button** that calls the uploader
4. **Handle errors** and provide user feedback
5. **Test with actual Maslow hardware**

## Usage Example

```javascript
// On abundance.maslowcnc.com
const uploader = new MaslowUploader('192.168.1.100');

// Upload a G-code file
const gcodeContent = 'G0 X0 Y0\nG1 X10 Y10 F1000\n';
const file = new Blob([gcodeContent], { type: 'text/plain' });

try {
    const result = await uploader.uploadToSD(
        file,
        'myproject.gcode',
        (percent) => console.log(`Progress: ${percent.toFixed(1)}%`)
    );
    console.log('Upload successful!', result);
} catch (error) {
    console.error('Upload failed:', error.message);
}
```

## Troubleshooting

### If CORS doesn't work:
1. Verify the Maslow firmware includes these changes
2. Check browser console for specific CORS errors
3. Ensure accessing from https://abundance.maslowcnc.com (not localhost)
4. Verify `credentials: 'include'` is set in fetch/XHR requests

### If uploads fail:
1. Check Maslow's IP address is correct
2. Verify Maslow is on and WiFi is connected
3. Check authentication status (may need to log in first)
4. Ensure SD card is inserted (for /upload endpoint)
5. Check available storage space

## Files Modified
- `firmware/FluidNC/src/WebUI/WebServer.cpp` - CORS implementation
- `firmware/FluidNC/src/WebUI/WebServer.h` - Function declarations
- `docs/API_FileUpload.md` - API documentation (new file)

## Commit History
1. Initial CORS implementation and API documentation
2. Fixed sendJSON to use code parameter correctly
