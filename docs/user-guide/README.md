# User Guide

This guide covers the essential features of your Maslow 4 CNC machine's web interface, including connecting to WiFi, uploading files, and running your first cuts.

![Maslow Web Interface](images/guide-01.png)

## Connecting to Your Maslow

### Step 1: Power On

When powered on, your Maslow 4 creates a WiFi network named `maslow`. The default password is `12345678`.

### Step 2: Connect to WiFi

Connect your computer, smartphone, or tablet to the `maslow` WiFi network.

![WiFi Connection](images/guide-02.jpg)

### Step 3: Access the Web Interface

The interface may auto-open when you connect. If not, enter `192.168.0.1` in your browser's address bar.

![Browser Access](images/guide-03.png)

## USB Control Options (Feasibility Analysis)

Maslow firmware already exposes a USB CDC serial interface on ESP32-S3 builds (`ARDUINO_USB_CDC_ON_BOOT=1`), so USB command/control is possible today. The remaining question is how to provide a **full UI over USB** when WiFi is unreliable.

### Summary of the three options

| Option | Does it work? | Estimated effort | ESP32-S3 fit/risk | Platform coverage |
| --- | --- | --- | --- | --- |
| Browser UI over USB serial (Web Serial) | **Yes, feasible** for browsers with Web Serial support | **Medium** (UI transport layer + robust reconnect/state handling) | **Low firmware risk** (reuses existing USB CDC and serial protocol) | Best on desktop Chromium browsers; iOS is currently not viable for Web Serial |
| USB network in firmware (ECM/RNDIS/NCM style) | **Technically feasible**, but most firmware/driver complexity | **High** (new USB networking class, IP routing/interface handling, cross-OS validation) | **Medium/High risk** (adds firmware complexity and USB stack surface area) | Potentially broad on desktop OSes, but mobile support is inconsistent/adapter-dependent |
| Shim layer (desktop app bridging USB serial to local web UI) | **Yes, feasible now** | **Medium** (desktop app/service + packaging/signing + updater path) | **Very low firmware risk** (no major firmware changes required) | Strong for Windows/macOS/Linux desktops; mobile requires a separate app strategy |

### Option 1: Browser UI over USB serial (`navigator.serial`)

This approach adds a serial transport path in ESP3D-WEBUI, mapping existing command/status traffic onto the current USB CDC serial channel.

- **Why it is feasible:** firmware already starts USB CDC at `115200`, and command handling already exists over serial.
- **Main work items:**
  1. Add a transport abstraction in the web UI so HTTP/WebSocket and Web Serial can share logic.
  2. Implement framing/parsing and command queueing over serial.
  3. Handle reconnect, error recovery, and flow control for long jobs.
- **Key limitation:** browser support (especially iOS/WebKit) is the blocker for universal device coverage.

### Option 2: USB network interface in firmware

This approach makes the board appear as a USB network adapter so the existing web UI (HTTP + WebSocket) can be used with minimal UI changes.

- **Why it is attractive:** preserves the current UI protocol model.
- **Main work items:**
  1. Add and validate a USB networking class in firmware.
  2. Bind network services to the USB network interface and define addressing behavior.
  3. Validate host drivers/routing behavior across Windows, Linux, macOS, Android, and iOS paths.
- **Primary risk:** significantly more firmware/USB-stack integration and more cross-platform edge cases than the other options.

### Option 3: Shim layer (desktop bridge)

This approach uses a desktop utility that talks USB serial to Maslow and exposes a local HTTP/WebSocket endpoint for the existing UI.

- **Why it is practical:** fastest path to a full UI-over-USB workflow on desktop platforms with minimal firmware impact.
- **Main work items:**
  1. Build a lightweight local bridge service/app.
  2. Package/sign/distribute for Windows, Linux, and macOS.
  3. Define connection UX and recovery for cable disconnect/reconnect.
- **Tradeoff:** strongest on desktop; mobile would need separate app integration.

### Recommendation

1. **Near-term:** implement **Option 3 (shim)** for fastest reliable desktop UX where WiFi is problematic.
2. **Parallel prototype:** evaluate **Option 1 (Web Serial)** for browser-only desktop workflows.
3. **Long-term:** pursue **Option 2 (USB networking)** only if broad mobile/browser parity is required and higher firmware complexity is acceptable.

## Web Interface Overview

The Maslow 4 web interface provides all the controls you need to operate your CNC machine without installing any additional software.

### Main Dashboard

The main dashboard displays:

- Machine status
- Current position (X, Y, Z coordinates)
- Connection status
- Quick access to common actions

![Main Dashboard](images/guide-04.png)

![Dashboard Overview](images/guide-05.png)

### Actions Menu

The Actions menu provides access to:

- **Home Position:** Define and return to the home position
- **Move Machine:** Manual jog controls for X, Y, and Z axes
- **Calibration:** Run the calibration wizard
- **Run G-code:** Execute uploaded G-code files

![Actions Menu](images/guide-06.png)

## Configuring WiFi (Optional)

You can configure Maslow to join your home WiFi for networked access from any device on your network.

### Steps to Connect to Home WiFi

1. Access the web interface via the default `maslow` network
2. Navigate to the "FluidNC" tab or "Network" settings
3. Enter your home WiFi credentials
4. Save and restart the machine

![WiFi Configuration](images/guide-10.png)

## USB Network Access (Experimental)

On ESP32-S3 builds, Maslow now also exposes a USB Ethernet (CDC-ECM) interface when connected by USB cable.

- Device address: `192.168.7.1`
- Intended use: opening the same web interface over USB when WiFi conditions are poor

If your host does not get an address automatically, set the host to a static address in `192.168.7.x` (for example `192.168.7.2`) and browse to `http://192.168.7.1`.

## Uploading and Running G-code Files

### Uploading Files

1. Navigate to the "Files" or "Actions" menu
2. Click "Upload files"
3. Select your `.nc` G-code file
4. Wait for the upload to complete

![File Upload](images/guide-12.png)

### Running a G-code File

1. Select the uploaded file from the list
2. Set the start position (or pick a specific line if resuming)
3. Click "Run" to start the job
4. Monitor progress from the dashboard

![Running G-code](images/guide-13.png)

![Job Progress](images/guide-14.png)

### Visualizing Tool Paths

The web interface can display a visual preview of the tool path, helping you verify the job before running.

![Tool Path Visualization](images/guide-15.png)

## Manual Machine Controls

### Jog Controls

Use the arrow buttons to manually move the machine:

- **X-axis:** Left and right movement
- **Y-axis:** Up and down movement (on vertical setup)
- **Z-axis:** Router depth control

![Jog Controls](images/guide-16.png)

![Movement Controls](images/guide-17.png)

### Zeroing Position

Set the current position as zero for any axis:

1. Move the machine to your desired zero point
2. Click the "Zero" button for the appropriate axis
3. Confirm the new zero position

![Zeroing Position](images/guide-18.png)

### Spindle Control

Control the router/spindle using built-in macros:

- **M3:** Spindle ON
- **M5:** Spindle OFF

![Spindle Control](images/guide-19.png)

## Extending and Retracting Belts

The belts can be extended or retracted using the controls in the interface.

![Belt Controls](images/guide-08.gif)

![Belt Extension](images/guide-26.gif)

## Updating Firmware

Keeping your firmware up to date ensures you have the latest features and bug fixes.

### Firmware Update Process

1. Download the latest firmware from [GitHub Releases](https://github.com/MaslowCNC/Maslow_4/releases):
   - `firmware.bin`
   - `index.html.gz`
   - `maslow.yaml`

2. Access the web interface and go to the "FluidNC" tab

3. Click "Update the Firmware" and select `firmware.bin`

4. Upload the other files using the file upload function

5. Restart the machine

![Firmware Update](images/guide-20.png)

![Update Progress](images/guide-21.png)

> **Note:** If updating from a version before 1.0 to after 1.0, a USB cable is required. See the official documentation for USB update instructions.

## Calibration

Access the calibration wizard from the Actions menu. The wizard will guide you through:

1. Belt extension and retraction
2. Home position definition
3. Anchor point measurement
4. Calibration grid setup
5. Automated calibration process

![Calibration Wizard](images/guide-22.png)

![Calibration Steps](images/guide-23.png)

![Calibration Grid](images/guide-24.png)

For detailed calibration instructions, see [Putting It All Together](../putting-it-all-together-4-1/README.md).

## Tips for Best Results

- **Work Surface:** Use a flat, rigid spoil board to protect your work surface
- **Material Hold-down:** Secure your material firmly to prevent movement during cutting
- **Start Slow:** Begin with conservative feed rates and speeds until you're familiar with the machine
- **Test Cuts:** Always do a test cut on scrap material before cutting your final piece
- **Dust Collection:** Connect a shop vacuum to the dust port for cleaner operation

![Cutting Tips](images/guide-25.png)

## LED Status Indicators

The Maslow 4 control board has two LEDs that communicate machine state at a glance.

| LED | Pattern | Meaning |
| --- | --- | --- |
| **Red** | Slow blink (300 ms) | Error or emergency stop — check the console for details, then power-cycle |
| **Red + WiFi** | Rapid double-blink | Motion-control watchdog fired — power-cycle the machine |
| **WiFi** | Short blinks on startup | Encoding the machine's IP address on the local network |

For a full explanation of each error condition see the [Troubleshooting Guide](../Troubleshooting.md#led-indicators).

## Troubleshooting

### Red LED is blinking

The Red LED blinks slowly (300 ms on/off) when the machine has detected an error. Open the web interface console to read the error message. Common causes are a disconnected encoder or motor cable, a belt that has gone slack, or an emergency stop triggered during a job. Power-cycle the machine after fixing the underlying problem.

If **both** the Red and WiFi LEDs flash together in a rapid double-blink, the motion-control watchdog fired. Power-cycle the machine.

### Cannot Connect to WiFi

- Verify you're connecting to the correct network (`maslow`)
- Check the default password (`12345678`)
- Try power cycling the machine

### Web Interface Not Loading

- Manually enter `192.168.0.1` in your browser
- Try a different browser
- Clear browser cache

### Machine Not Responding

- Check all cable connections
- Verify power is connected
- Check the serial connection in the web interface

![Troubleshooting](images/guide-27.png)

## Resources

- [Maslow CNC Forums](https://forums.maslowcnc.com/) - Community support and troubleshooting
- [Official Assembly Guide](https://www.maslowcnc.com/assembly-guide)
- [Firmware Repository](https://github.com/MaslowCNC/Firmware)
- [FluidNC Documentation](https://github.com/bdring/FluidNC)
