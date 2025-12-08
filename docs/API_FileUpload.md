# FluidNC File Upload API

This document describes how to upload G-code files to Maslow CNC machines running FluidNC firmware from external websites.

## CORS Configuration

The FluidNC firmware has been configured to accept cross-origin requests from:
- `https://abundance.maslowcnc.com`

This allows the Abundance website to upload G-code files directly to Maslow machines without requiring users to download files first.

## API Endpoints

### 1. Upload to SD Card: `/upload`

Uploads a file to the SD card storage.

**Method:** POST  
**Content-Type:** multipart/form-data

**Form Data:**
- File field: The file to upload
- `<filename>S` (optional): File size in bytes (where `<filename>` is the name of the uploaded file)
- `path` (optional): Destination directory path on the SD card

**Example using JavaScript:**

```javascript
async function uploadGCodeToSD(fileBlob, filename, maslowIP) {
    const formData = new FormData();
    formData.append(filename, fileBlob, filename);
    formData.append(`${filename}S`, fileBlob.size.toString());
    
    const response = await fetch(`http://${maslowIP}/upload`, {
        method: 'POST',
        body: formData,
        credentials: 'include', // Important for authentication cookies
    });
    
    if (!response.ok) {
        throw new Error(`Upload failed: ${response.statusText}`);
    }
    
    const result = await response.json();
    return result;
}

// Usage example
const file = new Blob(['G0 X0 Y0\nG1 X10 Y10\n'], { type: 'text/plain' });
uploadGCodeToSD(file, 'test.gcode', '192.168.1.100')
    .then(result => console.log('Upload successful:', result))
    .catch(error => console.error('Upload failed:', error));
```

### 2. Upload to Local Filesystem: `/files`

Uploads a file to the local flash filesystem (LocalFS).

**Method:** POST  
**Content-Type:** multipart/form-data

**Form Data:**
- File field: The file to upload
- `<filename>S` (optional): File size in bytes (where `<filename>` is the name of the uploaded file)
- `path` (optional): Destination directory path on LocalFS

**Example using JavaScript:**

```javascript
async function uploadGCodeToLocalFS(fileBlob, filename, maslowIP) {
    const formData = new FormData();
    formData.append(filename, fileBlob, filename);
    formData.append(`${filename}S`, fileBlob.size.toString());
    
    const response = await fetch(`http://${maslowIP}/files`, {
        method: 'POST',
        body: formData,
        credentials: 'include', // Important for authentication cookies
    });
    
    if (!response.ok) {
        throw new Error(`Upload failed: ${response.statusText}`);
    }
    
    const result = await response.json();
    return result;
}
```

## Authentication

FluidNC may require authentication depending on the configuration:
- Authentication is typically disabled by default (authentication level: ADMIN)
- If authentication is enabled, credentials must be provided via cookies
- Guest users cannot upload files

If you receive a 401 error, authentication is required. The user must first log in through the FluidNC web interface.

## Response Format

All responses are in JSON format.

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
            "name": "test.gcode",
            "shortname": "test.gcode",
            "size": 1234,
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

## Error Codes

- **401 Unauthorized:** Authentication failed - user must log in first
- **500 Internal Server Error:** Upload failed (see status message for details)
- **200 OK:** Success (even with failed status in JSON)

## Upload Progress

To track upload progress, add a progress event listener:

```javascript
async function uploadWithProgress(fileBlob, filename, maslowIP, onProgress) {
    return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        const formData = new FormData();
        
        formData.append(filename, fileBlob, filename);
        formData.append(`${filename}S`, fileBlob.size.toString());
        
        xhr.upload.addEventListener('progress', (e) => {
            if (e.lengthComputable) {
                const percentComplete = (e.loaded / e.total) * 100;
                onProgress(percentComplete);
            }
        });
        
        xhr.addEventListener('load', () => {
            if (xhr.status === 200) {
                resolve(JSON.parse(xhr.responseText));
            } else {
                reject(new Error(`Upload failed: ${xhr.statusText}`));
            }
        });
        
        xhr.addEventListener('error', () => {
            reject(new Error('Network error during upload'));
        });
        
        xhr.open('POST', `http://${maslowIP}/upload`);
        xhr.withCredentials = true; // Important for authentication
        xhr.send(formData);
    });
}

// Usage
uploadWithProgress(file, 'test.gcode', '192.168.1.100', (percent) => {
    console.log(`Upload progress: ${percent.toFixed(2)}%`);
})
.then(result => console.log('Upload complete:', result))
.catch(error => console.error('Upload failed:', error));
```

## Complete Example for abundance.maslowcnc.com

Here's a complete implementation that handles errors and provides user feedback:

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

// Example usage:
const uploader = new MaslowUploader('192.168.1.100');

// Check if Maslow is reachable
if (await uploader.isReachable()) {
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
} else {
    console.error('Cannot reach Maslow. Check IP address and network connection.');
}
```

## Important Notes

1. **Network Configuration:** The Maslow must be on the same network as the computer running the abundance website, or be accessible via the internet (not recommended for security reasons).

2. **IP Address Discovery:** Users will need to know their Maslow's IP address. This can typically be found:
   - On the Maslow's display (if equipped)
   - In the WiFi router's connected devices list
   - By checking the ESP32's serial output during boot

3. **File Size Limits:** The SD card and LocalFS have different size limits:
   - SD card: Limited by card capacity
   - LocalFS: Limited by ESP32 flash (typically a few MB available)

4. **Filename Restrictions:** Avoid special characters in filenames. Stick to alphanumeric characters, hyphens, and underscores.

5. **HTTPS Note:** If abundance.maslowcnc.com is served over HTTPS, the browser may block requests to HTTP endpoints (the Maslow). You may need to:
   - Inform users about this limitation
   - Provide instructions for allowing mixed content
   - Consider implementing HTTPS on the Maslow (complex)

## Troubleshooting

### CORS Errors
If you see CORS errors in the browser console:
- Verify the Maslow firmware includes the CORS changes
- Check that you're accessing from `https://abundance.maslowcnc.com`
- Ensure the `credentials: 'include'` or `withCredentials: true` option is set

### 401 Authentication Error
- The Maslow has authentication enabled
- User must first log in through the Maslow's web interface
- After login, cookies will be automatically included in requests

### Network Errors
- Verify the Maslow's IP address is correct
- Check that both devices are on the same network
- Verify the Maslow is powered on and WiFi is connected
- Check firewall settings aren't blocking the connection

### Upload Fails with "Not Enough Space"
- The SD card or LocalFS is full
- Delete old files through the Maslow's web interface
- Use a larger SD card if needed
