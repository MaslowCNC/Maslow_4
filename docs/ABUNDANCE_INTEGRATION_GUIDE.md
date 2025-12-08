# Information for abundance.maslowcnc.com Integration

## API Endpoints

Your site can now upload G-code files directly to Maslow machines using these endpoints:

### Upload to SD Card
**Endpoint:** `POST http://<maslow-ip>/upload`

### Upload to Local Flash Storage
**Endpoint:** `POST http://<maslow-ip>/files`

## CORS Configuration

The FluidNC firmware has been configured to accept requests from:
- **Origin:** `https://abundance.maslowcnc.com`

## Complete Implementation Example

Copy and use this JavaScript class in your website:

```javascript
class MaslowUploader {
    constructor(maslowIP) {
        this.maslowIP = maslowIP;
        this.baseURL = `http://${maslowIP}`;
    }
    
    /**
     * Upload a G-code file to the Maslow's SD card
     * @param {Blob|File} file - The file to upload
     * @param {string} filename - Desired filename on the Maslow
     * @param {Function} onProgress - Optional progress callback (receives percent 0-100)
     * @returns {Promise<Object>} Upload result
     */
    async uploadToSD(file, filename, onProgress = null) {
        return this._upload('/upload', file, filename, onProgress);
    }
    
    /**
     * Upload a file to the Maslow's local filesystem
     * @param {Blob|File} file - The file to upload
     * @param {string} filename - Desired filename on the Maslow
     * @param {Function} onProgress - Optional progress callback (receives percent 0-100)
     * @returns {Promise<Object>} Upload result
     */
    async uploadToLocalFS(file, filename, onProgress = null) {
        return this._upload('/files', file, filename, onProgress);
    }
    
    _upload(endpoint, file, filename, onProgress) {
        return new Promise((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            const formData = new FormData();
            
            // Add the file and size parameter
            formData.append(filename, file, filename);
            formData.append(`${filename}S`, file.size.toString());
            
            // Setup progress tracking
            if (onProgress && typeof onProgress === 'function') {
                xhr.upload.addEventListener('progress', (e) => {
                    if (e.lengthComputable) {
                        const percentComplete = (e.loaded / e.total) * 100;
                        onProgress(percentComplete);
                    }
                });
            }
            
            // Handle completion
            xhr.addEventListener('load', () => {
                if (xhr.status === 200) {
                    try {
                        const result = JSON.parse(xhr.responseText);
                        if (result.status && result.status.includes('failed')) {
                            reject(new Error(result.status));
                        } else {
                            resolve(result);
                        }
                    } catch (e) {
                        reject(new Error('Invalid response from Maslow'));
                    }
                } else if (xhr.status === 401) {
                    reject(new Error('Authentication required. Please log in to the Maslow web interface first.'));
                } else {
                    reject(new Error(`Upload failed: ${xhr.status} ${xhr.statusText}`));
                }
            });
            
            // Handle errors
            xhr.addEventListener('error', () => {
                reject(new Error('Network error. Check that the Maslow is powered on and connected.'));
            });
            
            xhr.addEventListener('abort', () => {
                reject(new Error('Upload cancelled'));
            });
            
            // Send the request
            xhr.open('POST', `${this.baseURL}${endpoint}`);
            xhr.withCredentials = true; // Important for authentication
            xhr.send(formData);
        });
    }
    
    /**
     * Test if the Maslow is reachable
     * @returns {Promise<boolean>}
     */
    async isReachable() {
        try {
            const response = await fetch(`${this.baseURL}/`, {
                method: 'HEAD',
                credentials: 'include',
            });
            return response.ok;
        } catch (e) {
            return false;
        }
    }
}
```

## Usage Example

```javascript
// User provides their Maslow's IP address
const maslowIP = '192.168.1.100'; // Get this from user input

// Create uploader instance
const uploader = new MaslowUploader(maslowIP);

// Check if Maslow is reachable
const isOnline = await uploader.isReachable();
if (!isOnline) {
    alert('Cannot connect to Maslow. Please check the IP address.');
    return;
}

// Upload a G-code file
const gcodeBlob = new Blob([gcodeContent], { type: 'text/plain' });

try {
    const result = await uploader.uploadToSD(
        gcodeBlob,
        'myproject.gcode',
        (percent) => {
            // Update progress bar
            updateProgressBar(percent);
        }
    );
    
    console.log('Upload successful!', result);
    alert('File uploaded successfully!');
} catch (error) {
    console.error('Upload failed:', error);
    alert(`Upload failed: ${error.message}`);
}
```

## Required User Input

Your website will need to collect:
1. **Maslow IP Address** - Users need to know their Maslow's IP (found on the device display, WiFi router, or serial output)
2. **Filename** - What to name the file on the Maslow (default to project name + .gcode)

## Response Format

**Success Response:**
```json
{
    "status": "Ok",
    "path": "",
    "total": "8.00 MB",
    "used": "2.45 MB",
    "occupation": 30,
    "files": [
        {
            "name": "myproject.gcode",
            "shortname": "myproject.gcode",
            "size": 12345,
            "datetime": ""
        }
    ]
}
```

**Error Response:**
```json
{
    "status": "Upload failed"
}
```

## Error Handling

| Status Code | Meaning | Action |
|------------|---------|--------|
| 200 | Success | File uploaded |
| 401 | Authentication required | User must log in to Maslow web UI first |
| 500 | Server error | Check status message in JSON response |

## Important Notes

1. **Network Requirement:** The Maslow must be on the same network as the user's computer, OR be accessible via the internet (not recommended).

2. **IP Discovery:** Help users find their Maslow's IP:
   - Check the Maslow's display (if equipped)
   - Look in WiFi router's connected devices
   - Check serial output during Maslow boot

3. **File Size Limits:**
   - SD card: Limited by card capacity (typically GB)
   - LocalFS: Limited by ESP32 flash (typically a few MB)

4. **Authentication:** Most Maslow setups have authentication disabled, but if enabled, users must log in through the Maslow's web interface first.

5. **HTTPS Mixed Content:** Since abundance.maslowcnc.com uses HTTPS but Maslow uses HTTP, browsers may block the requests. You should:
   - Warn users about this in your UI
   - Provide instructions for allowing mixed content
   - Consider making this feature opt-in with a warning

## Browser Compatibility

This implementation uses:
- XMLHttpRequest (widely supported)
- FormData (widely supported)
- Promises (ES6+, polyfill available)
- Fetch API (for reachability check, modern browsers)

## Testing Recommendations

1. Test with actual Maslow hardware
2. Test with different browsers (Chrome, Firefox, Safari, Edge)
3. Test error scenarios (wrong IP, no SD card, auth required)
4. Test large files (ensure progress callback works)
5. Test network errors (disconnect during upload)

## Full Documentation

For complete API documentation including troubleshooting, see:
- [API_FileUpload.md](./API_FileUpload.md) - Complete API reference
- [CORS_Implementation_Summary.md](./CORS_Implementation_Summary.md) - Technical implementation details

## Questions?

If you need clarification or encounter issues:
1. Check the full API documentation
2. Open an issue on the Maslow_4 GitHub repository
3. Ask in the Maslow CNC community forums
