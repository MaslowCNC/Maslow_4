# FluidNC ESP32 CNC Firmware Development

FluidNC is a CNC firmware optimized for ESP32 controllers. It's the next generation of firmware from the creators of Grbl_ESP32, featuring a built-in web UI and the flexibility to operate a wide variety of machine types including laser/spindle combinations and tool changers.

This project is a fork from the original FluidNC project specifically for Maslow CNC machines. It is not important to maintain compatability with other kinds of machines, this fork is specific to Maslow CNC type machines.

**The repository now includes ESP3D-WEBUI**, which was previously a separate project. This is the primary web interface for controlling the Maslow CNC machine through FluidNC firmware.

**ALWAYS reference these instructions first and fallback to search or bash commands only when you encounter unexpected information that does not match the info here.**

## Maslow architecture rules (read before changing the web UI or the firmware ↔ UI protocol)

These are deliberate design decisions. A change that breaks one of them will be sent back, even if
it works. If a task seems to need breaking one, say so in the PR and explain why, instead of
working around it quietly.

### The UI talks to the machine through `machine.js`

- **Every new machine action goes through `ESP3D-WEBUI/www/js/machine.js`.** It is the one API for
  moving, belt operations, jobs, stopping and settings. Its actions return an **Operation**: a
  Promise that settles when the action has finished, plus `.accepted`, which settles when the
  firmware has taken the command. Failures reject with an error carrying a `reason` (`rejected`,
  `not-sent`, `disconnected`, `timeout`, `reset`, `cancelled`, `failed`, `not-ready`) and a
  `detail` message. UI code shows progress from `.accepted` and the result from the Operation
  itself, as the Apply Tension button in `tablet.js` (`runButtonAction`) does.
- **Don't send commands from UI code with `sendCommand`, `SendPrinterCommand` or raw
  WebSocket/HTTP calls.** About 60 older calls still do this. They are being moved over one feature
  at a time. When you change a feature that uses one, move that feature onto `machine.js` (adding
  an action there if needed). Don't migrate unrelated calls in the same PR.
- **Reuse an existing action or `$` command before adding one.** The firmware's `$` commands are
  listed in `docs/protocol.md` (section 4) and registered in
  `firmware/FluidNC/src/ProcessSettings.cpp`. A new action in `machine.js` usually needs no new
  firmware command.
- All line commands go over the WebSocket, one at a time, through `wscommands.js`, which matches
  each reply to its line. Only `[ESP…]` commands and file transfers use HTTP. Don't add a second
  path for commands.

### Machine state comes from the status report

- **Read machine state from `machine.state` and `machine.on("state", fn)`**, which are filled from
  the GRBL status report the firmware sends about every 50 ms. That includes the Maslow state
  (`MS`), belt lengths and errors (`BL`, `BE`), motor currents (`MC`), job progress (`SD`) and a
  stopped job (`STOPPED`). `docs/protocol.md` section 5.1 lists every field.
- **Don't add new state reads that use `MINFO` or parse log lines.** Both are being phased out for
  state. When the UI needs state it doesn't have, add a field to the status report in
  `firmware/FluidNC/src/Report.cpp` (keep it short: the report is sent every 50 ms), parse it in
  `machine.js`, and document it in `docs/protocol.md`.
- **Report size is sensitive, so report changes need a human reviewer.** The report goes out about
  every 50 ms over WiFi, and a longer report means more dropped frames and less headroom. A PR that
  adds data to the regular status report (a new field, or a longer value in an existing one) must
  not be merged until @tristan-huber or @BarbourSmith has reviewed it. Mention @tristan-huber in
  the PR description, say which field changed, and give its typical size in bytes
  (`scripts/report_monitor.mjs` measures report size and drops). Prefer a field that is sent only
  while it applies (like `STOPPED`) over one sent in every report.
- Some log lines are still matched by their exact text in the UI (`maslow.js`, `debugging.js`).
  Before changing any `log_info` / `log_warn` / `log_error` text, search `ESP3D-WEBUI/www/js` for
  it and change both sides in the same PR.
- The calibration state numbers (`firmware/FluidNC/src/Maslow/MaslowEnums.h`, 0–9) are mirrored
  in `machine.js` (`MASLOW`) and range-checked in the UI. Adding a state means updating both.

### Stopping and jobs

- **There is no Pause button, on purpose.** Stop is `machine.stop()`: a soft reset (0x18), which
  halts at once, then `$STOP` (raise Z, save positions). A stopped SD job is reported as
  `STOPPED:<line>,<file>` and resumed with `machine.resumeFile(file, line)` (`$SD/RunFrom`). Don't
  bring back feed-hold pause or add another stop path.

### Safety and tests

- Never send motion, belt or calibration commands to a machine as a side effect of other work.
- A change to `machine.js` or `wscommands.js` needs bun tests in `ESP3D-WEBUI/www/js/tests`
  (`cd ESP3D-WEBUI && bun test`). One known failure (`maslowInfoMsgHandling is not defined`)
  predates this work.
- Known bugs and loose ends are listed in `BACKLOG.md` at the repository root. Check it before
  starting, and remove an entry in the same PR that fixes it.

## Working Effectively

### Bootstrap and Setup
- Install PlatformIO: `pip3 install --upgrade platformio`
- Bootstrap project dependencies: `pio platform install` (runs automatically on first build)
- NEVER CANCEL long-running builds or commands - wait for completion

### Build System Commands
- **Main firmware build**: `pio run -e wifi_s3` -- takes ~2-3 minutes. NEVER CANCEL. Set timeout to 5+ minutes.
- **Clean build**: `pio run -e wifi_s3 -t clean` -- takes ~1 second
- **Filesystem build**: `pio run -e wifi_s3 -t buildfs` -- takes ~4 seconds
- **Upload firmware**: `pio run -e wifi_s3 -t upload` (requires connected ESP32)
- **Upload filesystem**: `pio run -e wifi_s3 -t uploadfs` (requires connected ESP32)

### Web UI Build Systems

This repository has TWO web UI systems:

#### 1. ESP3D-WEBUI (Primary Web Interface)
- **Location**: `ESP3D-WEBUI/` directory (root level)
- **Description**: The main web interface for FluidNC/Maslow, previously a separate project now integrated into this repository
- **Prerequisites**: Node.js (v20+) and npm are required
- **Install dependencies**: `cd ESP3D-WEBUI && npm install` -- takes ~13 seconds. NEVER CANCEL.
- **Build commands**:
  - **English only (recommended)**: `gulp package --lang en` -- produces ~122KB output
  - **All languages**: `gulp package` -- produces ~150KB+ output (may be too large for ESP32)
  - **NPM shortcuts**: `npm run build:en` (English) or `npm run build` (all languages)
- **Output**: `dist/index.html.gz` - Upload this to ESP32
- **Testing**: `npm run start` - Builds English and starts local test server
- **CRITICAL**: Single language builds recommended due to ESP32 storage limits
- **Documentation**: See `ESP3D-WEBUI/COMPILATION.md` and `ESP3D-WEBUI/HOWTO-Test-Locally.md`

#### 2. Embedded Tool UI (Auxiliary Interface)
- **Location**: `firmware/embedded/` directory
- **Description**: Auxiliary tool interface for specific functionality
- **Prerequisites**: Node.js (v20+) and npm are required
- **Install dependencies**: `cd firmware/embedded && npm install` -- takes ~13 seconds. NEVER CANCEL.
- **Build command**: `cd firmware/embedded && python3 build.py` -- takes ~8 seconds
- **Output**: `firmware/embedded/tool.html.gz`
- **CRITICAL**: Always rebuild after changes to `firmware/embedded/www/*` files

### Available Build Environments
- `wifi_s3` (default) - ESP32-S3 with WiFi
- `bt_s3` - ESP32-S3 with Bluetooth
- `wifibt_s3` - ESP32-S3 with WiFi and Bluetooth
- `noradio_s3` - ESP32-S3 without wireless
- `wifi` - ESP32 with WiFi
- `bt` - ESP32 with Bluetooth
- `wifibt` - ESP32 with WiFi and Bluetooth
- `noradio` - ESP32 without wireless

## Pull Request Best Practices

### Minimizing Changes
- **Scope changes narrowly**: Only modify files directly related to the feature/fix being implemented
- **Avoid formatting-only changes**: Do not include whitespace or formatting changes in files unrelated to your functional changes
- **Surgical modifications**: Make the smallest possible changes to achieve the desired functionality
- **Review diff before committing**: Always check `git diff` to ensure only intended changes are included
- **Remove trailing whitespace**: Always remove trailing whitespace from modified lines before committing
- **Clean up dead code**: Remove any unused code, variables, or functions that your changes make obsolete

### Code Formatting Guidelines
- **Format selectively**: Only run clang-format on files you have functionally modified
- **Avoid global formatting**: Do NOT run formatting tools across the entire repository
- **IDE auto-format**: Configure IDE to only format files being actively edited
- **Pre-commit review**: Check that formatting changes are limited to functionally modified files

### Code Quality Checks (REQUIRED for every commit)
- **Trailing whitespace removal**: Before every commit, check all modified files for trailing whitespace and remove it
  - Use `git diff --check` to identify trailing whitespace issues
  - Remove trailing spaces from the end of lines in all modified files
  - Ensure no trailing whitespace is introduced in new code
- **Dead code detection**: After making changes, analyze the modified files to identify and remove dead/unreachable code
  - Check for unused variables, functions, or classes introduced or left behind by your changes
  - Verify that all code paths are reachable
  - Remove commented-out code blocks unless they serve as important documentation
  - Remove unused imports and includes that become unnecessary after your changes
  - Use compiler warnings to identify unused code (e.g., `-Wunused-variable`, `-Wunused-function`)
- **Pre-commit validation**: Before using `report_progress`, always:
  1. Run `git diff --check` to detect trailing whitespace
  2. Review the diff for any unreachable or dead code introduced by your changes
  3. Fix any issues before committing

## Testing and Validation

### Unit Tests (Currently Broken)
- **Command**: `pio test -e native`
- **Status**: Unit tests currently fail due to FreeRTOS compatibility issues on native platform
- **Known Issues**: Missing headers (Queue.h case sensitivity), OLED library dependencies, FreeRTOS type mismatches
- **Recommendation**: Do NOT rely on native unit tests. Test on actual hardware instead.

### Code Formatting
- **Format code**: Use clang-format with provided `.clang-format` configuration
- **IDE Integration**: Most IDEs automatically pick up `.clang-format` file
- **Manual format**: `clang-format -i <filename>` for individual files
- **CRITICAL**: Always format code before committing to maintain project standards
- **MINIMIZE WHITESPACE CHANGES**: Only format files that you are functionally modifying
  - Do NOT run global formatting commands across the entire codebase
  - Do NOT format files that are not directly related to your changes
  - Focus formatting only on files where you made functional code changes
  - Avoid unnecessary whitespace-only changes that increase PR noise and review burden

## Validation Scenarios

### After Making Changes
1. **Build test**: `pio run -e wifi_s3` (must succeed without errors)
2. **Filesystem test**: `pio run -e wifi_s3 -t buildfs` (must succeed)
3. **Web UI tests** (if web changes made):
   - **ESP3D-WEBUI**: `cd ESP3D-WEBUI && gulp package --lang en` (for main interface changes)
   - **Embedded tool**: `cd firmware/embedded && python3 build.py` (for tool interface changes)
4. **Format test**: Run clang-format ONLY on C++ files you functionally modified (not entire codebase)
5. **Trailing whitespace check**: Run `git diff --check` to detect and fix trailing whitespace
6. **Dead code review**: Review changes for unreachable code, unused variables/functions, and remove them
7. **Clean build test**: `pio run -e wifi_s3 -t clean && pio run -e wifi_s3` (final verification)

### Hardware Testing Requirements
- **Upload test**: Flash firmware to ESP32 using `pio run -e wifi_s3 -t upload`
- **Web UI test**: Connect to FluidNC WiFi AP or local network, access web interface
- **Basic functionality**: Test G-code commands through web interface or serial connection
- **Configuration test**: Upload and verify YAML configuration files work correctly

## Configuration System

### Machine Definitions
- **Location**: Configuration files stored in `FluidNC/data/` directory
- **Format**: YAML files (e.g., `maslow.yaml`, `config-bak.yaml`)
- **Default**: `config.yaml` (can be changed with `$Config/Filename=<name.yaml>`)
- **Upload**: Use web interface or `pio run -e wifi_s3 -t uploadfs` to upload configurations

### Key Configuration Areas
- **Machine geometry**: Motor definitions, axis configuration
- **I/O mapping**: Pin assignments for steppers, spindles, limit switches
- **Network settings**: WiFi credentials, web server configuration
- **Motion control**: Stepping engine, acceleration, feed rates

## Common Development Tasks

### Project Structure Navigation
- **Main firmware**: `firmware/FluidNC/src/` - Core C++ firmware code
- **Primary web interface**: `ESP3D-WEBUI/` - Main web UI for FluidNC/Maslow (formerly separate project)
  - Source files: `ESP3D-WEBUI/www/` - HTML/CSS/JS source files
  - Build output: `ESP3D-WEBUI/dist/` - Compiled web interface
- **Auxiliary tool interface**: `firmware/embedded/` - Tool UI build system
  - Source files: `firmware/embedded/www/` - HTML/CSS/JS source files
- **Configurations**: `firmware/FluidNC/data/` - Example machine configurations
- **Libraries**: `firmware/libraries/` - Custom libraries and dependencies
- **Build output**: `firmware/.pio/build/wifi_s3/` - Compiled firmware and filesystem
- **Install scripts**: `firmware/install_scripts/` - Platform-specific installation helpers

### Adding New Features
1. **C++ changes**: Modify files in `firmware/FluidNC/src/`
2. **Web UI changes**:
   - **Main interface**: Modify files in `ESP3D-WEBUI/www/`, then run `cd ESP3D-WEBUI && gulp package --lang en`
   - **Tool interface**: Modify files in `firmware/embedded/www/`, then run `cd firmware/embedded && python3 build.py`
3. **Configuration changes**: Update YAML schema and parsing code
4. **Testing**: Build and test on hardware - native tests are not reliable

### Build Troubleshooting
- **Missing libraries**: PlatformIO automatically installs dependencies from `platformio.ini`
- **Web UI issues**:
  - ESP3D-WEBUI: Ensure `ESP3D-WEBUI/dist/index.html.gz` exists after running `gulp package --lang en`
  - Embedded tool: Ensure `firmware/embedded/tool.html.gz` exists after running `python3 build.py`
- **Upload failures**: Check USB connection, ESP32 in download mode (hold BOOT button)
- **Memory issues**: Use `max_littlefs.csv` partition table for 8MB ESP32-S3 boards
- **Web UI too large**: Use single language build for ESP3D-WEBUI (`gulp package --lang en` instead of `gulp package`)

## Install Scripts and Distribution

### Release Building
- **Build release**: `python3 build-release.py` (requires esptool and platform-specific setup)
- **Output location**: `release/` directory contains installable packages
- **Install scripts**: Generated for Windows (`win64/`) and POSIX (`posix/`) platforms

### Manual Installation (Development)
- **Erase flash**: `pio run -e wifi_s3 -t erase`
- **Upload all**: `pio run -e wifi_s3 -t upload && pio run -e wifi_s3 -t uploadfs`
- **Terminal access**: `pio device monitor` for serial debugging

## Critical Timing Information

- **Firmware build**: ~2-3 minutes initial, ~30-60 seconds incremental. NEVER CANCEL. Use 5+ minute timeout.
- **ESP3D-WEBUI build**: ~10-15 seconds for English, ~30+ seconds for all languages. NEVER CANCEL. Use 2+ minute timeout.
- **Embedded tool UI build**: ~8 seconds. NEVER CANCEL. Use 2+ minute timeout.
- **Filesystem build**: ~4 seconds. NEVER CANCEL. Use 1+ minute timeout.
- **Dependencies install**: ~13 seconds for npm (both ESP3D-WEBUI and embedded), ~3 minutes for PlatformIO. NEVER CANCEL.
- **Clean operations**: ~1 second.

## Dependencies and Prerequisites

### Required Tools
- **Python 3.x**: For PlatformIO and build scripts
- **PlatformIO**: `pip3 install --upgrade platformio`
- **Node.js v20+**: For web UI build system
- **clang-format**: For code formatting (usually available via package manager)

### Hardware Requirements
- **ESP32-S3**: Recommended platform (8MB flash, 240MHz, 320KB RAM)
- **ESP32**: Also supported with appropriate environment selection
- **USB connection**: For flashing and debugging

## Known Limitations and Workarounds

### Unit Testing
- **Issue**: Native tests fail due to FreeRTOS/ESP32 API dependencies
- **Workaround**: Test functionality on actual hardware instead
- **Status**: This is a known limitation, not a development priority

### Web UI Dependencies
- **Issue**: Node.js build system has security vulnerabilities in old packages
- **Impact**: Does not affect runtime security, only build-time warnings
- **Action**: Ignore npm audit warnings during development
- **Note**: Both ESP3D-WEBUI and firmware/embedded have their own node_modules and package.json

### Case Sensitivity
- **Issue**: Some file systems may have case sensitivity issues with include files
- **Workaround**: Use exact case matching in includes, verify on Linux systems

## File Types and Formats

### Source Code
- **C++ firmware**: `.cpp`, `.h` files using clang-format standards
- **Configuration**: `.yaml` files for machine definitions
- **Web assets**:
  - Main interface: `.html`, `.css`, `.js` files in `ESP3D-WEBUI/www/`
  - Tool interface: `.html`, `.css`, `.js` files in `firmware/embedded/www/`

### Build Artifacts (Do NOT commit)
- **Binary firmware**: `firmware/.pio/build/*/firmware.bin`
- **Filesystem image**: `firmware/.pio/build/*/littlefs.bin`
- **Compressed web**:
  - Main interface: `ESP3D-WEBUI/dist/index.html.gz`
  - Tool interface: `firmware/embedded/tool.html.gz`
  - Deployed: `firmware/FluidNC/data/index.html.gz`
- **Dependencies**: `firmware/.pio/libdeps/`, `ESP3D-WEBUI/node_modules/`, `firmware/embedded/node_modules/`
