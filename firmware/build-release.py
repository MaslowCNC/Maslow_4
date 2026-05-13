#!/usr/bin/env python3

# Build FluidNC release bundles (.zip files) for each host platform

from shutil import copy
from zipfile import ZipFile, ZipInfo
import subprocess, os, sys, shutil
import urllib.request

verbose = '-v' in sys.argv

environ = dict(os.environ)

# ============================================================================
# User-configurable variables - modify these paths as needed for your system 
# ============================================================================

# Path to platformio executable
# If pio is in your PATH, you can use just "pio"
# Otherwise, specify the full path to the pio or platformio executable
PLATFORMIO_CMD = "pio"

# Path to PlatformIO home directory (where packages are installed)
# Typically ~/.platformio on Linux/Mac or %USERPROFILE%\.platformio on Windows
# Set to None to use default location: os.path.join(os.path.expanduser('~'), '.platformio')
PLATFORMIO_HOME = None

# ============================================================================
# End of user-configurable variables
# ============================================================================

# Set platformio home directory
if PLATFORMIO_HOME is None:
    PLATFORMIO_HOME = os.path.join(os.path.expanduser('~'), '.platformio')

# Auto-detect platformio command if the configured one is not found
def find_platformio_cmd():
    """Try to find a working platformio command."""
    # Try the configured command first
    try:
        result = subprocess.run([PLATFORMIO_CMD, '--version'],
                              capture_output=True, timeout=5)
        if result.returncode == 0:
            return PLATFORMIO_CMD
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass

    # Try common alternatives
    alternatives = [
        'pio',
        'platformio',
        os.path.join(PLATFORMIO_HOME, 'penv', 'bin', 'platformio'),
        os.path.join(PLATFORMIO_HOME, 'penv', 'Scripts', 'platformio.exe'),
    ]

    for cmd in alternatives:
        try:
            result = subprocess.run([cmd, '--version'],
                                  capture_output=True, timeout=5)
            if result.returncode == 0:
                print(f"Auto-detected platformio command: {cmd}")
                return cmd
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue

    # If nothing works, return the configured command and let it fail with a clear error
    return PLATFORMIO_CMD

# Use the detected or configured command
PLATFORMIO_CMD = find_platformio_cmd()

# Extract version from git tag
try:
    git_tag = (
        subprocess.check_output(["git", "describe", "--tags", "--abbrev=0"], stderr=subprocess.DEVNULL)
        .strip()
        .decode("utf-8")
    )
    # Remove 'v' prefix if present (e.g., "v1.13" -> "1.13")
    version = git_tag.lstrip('v')
except (subprocess.CalledProcessError, FileNotFoundError):
    # Fallback to a default version if git command fails (no tags, not a git repo, git not installed, etc.)
    version = "unknown"
    print("Warning: Could not determine version from git tags, using 'unknown'")

# Change to the directory where this script is located (project root)
script_dir = os.path.dirname(os.path.realpath(__file__))
os.chdir(script_dir)

# Change path to the project folder (the folder with platformio.ini)
tag = "maslow4-"+version
sharedPath = 'install_scripts'


def buildEmbeddedPage():
    print('Building embedded web page')
    return subprocess.run(["python3", "build.py"], cwd="embedded").returncode

def buildEnv(pioEnv, verbose=False, extraArgs=None):
    cmd = [PLATFORMIO_CMD,'run', '--disable-auto-clean', '-e', pioEnv]
    if extraArgs:
        cmd.append(extraArgs)
    displayName = pioEnv
    print('Building firmware for ' + displayName)
    if verbose:
        app = subprocess.Popen(cmd, env=environ)
    else:
        app = subprocess.Popen(cmd, env=environ, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for line in app.stdout:
            line = line.decode('utf8')
            if "Took" in line or 'Uploading' in line or ("error" in line.lower() and "Compiling" not in line):
                print(line, end='')
    app.wait()
    print()
    return app.returncode

def buildFs(pioEnv, verbose=verbose, extraArgs=None):
    cmd = [ PLATFORMIO_CMD ,'run', '--disable-auto-clean', '-e', pioEnv, '-t', 'buildfs']
    if extraArgs:
        cmd.append(extraArgs)
    print('Building file system for ' + pioEnv)
    if verbose:
        app = subprocess.Popen(cmd, env=environ)
    else:
        app = subprocess.Popen(cmd, env=environ, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for line in app.stdout:
            line = line.decode('utf8')
            if "Took" in line or 'Uploading' in line or ("error" in line.lower() and "Compiling" not in line):
                print(line, end='')
    app.wait()
    print()
    return app.returncode


def copyToZip(zipObj, platform, fileName, destPath, mode=0o100755):
    sourcePath = os.path.join(sharedPath, platform, fileName)
    with open(sourcePath, 'r') as f:
        bytes = f.read()
    info = ZipInfo.from_file(sourcePath, os.path.join(destPath, fileName))
    info.external_attr = mode << 16
    zipObj.writestr(info, bytes)


relPath = os.path.join('release')
print("Creating release directory ", relPath)
if not os.path.exists(relPath):
    os.makedirs(relPath)

# We avoid doing this every time, instead checking in a new NoFile.h as necessary
# if buildEmbeddedPage() != 0:
#    sys.exit(1)

if buildFs('wifi_s3', verbose=verbose) != 0:
    sys.exit(1)

for envName in ['wifi_s3']:
    if buildEnv(envName, verbose=verbose) != 0:
        sys.exit(1)
    shutil.copy(os.path.join('.pio', 'build', envName, 'firmware.elf'), os.path.join(relPath, envName + '-' + 'firmware.elf'))

for platform in ['win64', 'posix']:
    print("Creating zip file for", platform)
    terseOSName = {
        'win64': 'win',
        'posix': 'posix',
    }
    scriptExtension = {
        'win64': '.bat',
        'posix': '.sh',
    }
    exeExtension = {
        'win64': '.exe',
        'posix': '',
    }
    withEsptoolBinary = {
        'win64': True,
        'posix': False,
    }

    zipDirName = os.path.join('fluidnc-' + tag + '-' + platform)
    zipFileName = os.path.join(relPath, zipDirName + '.zip')
    print("Creating zip file ", zipFileName)
    with ZipFile(zipFileName, 'w') as zipObj:
        name = 'HOWTO-INSTALL.txt'
        zipObj.write(os.path.join(sharedPath, platform, name), os.path.join(zipDirName, name))

        pioPath = os.path.join('.pio', 'build')

        # Put boot_app binary in the archive
        bootapp = 'boot_app0.bin';
        tools = os.path.join(PLATFORMIO_HOME,'packages','framework-arduinoespressif32','tools')
        zipObj.write(os.path.join(tools, "partitions", bootapp), os.path.join(zipDirName, 'common', bootapp))
        for secFuses in ['SecurityFusesOK.bin', 'SecurityFusesOK0.bin']:
            zipObj.write(os.path.join(sharedPath, 'common', secFuses), os.path.join(zipDirName, 'common', secFuses))

        # Put FluidNC binaries, partition maps, and installers in the archive
        for envName in ['wifi_s3']:

            # Put bootloader binaries in the archive
            bootloader = 'bootloader.bin'
            zipObj.write(os.path.join(pioPath, envName, bootloader), os.path.join(zipDirName, envName, bootloader))

            # Put littlefs.bin and index.html.gz in the archive
            # bt does not need a littlefs.bin because there is no use for index.html.gz
            if envName == 'wifi_s3':
                name = 'littlefs.bin'
                zipObj.write(os.path.join(pioPath, envName, name), os.path.join(zipDirName, envName, name))
                name = 'index.html.gz'
                zipObj.write(os.path.join('FluidNC', 'data', name), os.path.join(zipDirName, envName, name))

            objPath = os.path.join(pioPath, envName)
            for obj in ['firmware.bin','partitions.bin']:
                zipObj.write(os.path.join(objPath, obj), os.path.join(zipDirName, envName, obj))

            # E.g. posix/install-wifi.sh -> install-wifi.sh
            copyToZip(zipObj, platform, 'install-' + envName + scriptExtension[platform], zipDirName)

        for script in ['full-install','install-fs', 'fluidterm', 'checksecurity', 'erase', 'tools']:
            # E.g. posix/fluidterm.sh -> fluidterm.sh
            copyToZip(zipObj, platform, script + scriptExtension[platform], zipDirName)

        if platform == 'posix':
            copyToZip(zipObj, platform, 'full-install.command', zipDirName)

        # Put the fluidterm code in the archive
        for obj in ['fluidterm.py', 'README-FluidTerm.md']:
            fn = os.path.join('fluidterm', obj)
            zipObj.write(fn, os.path.join(zipDirName, os.path.join('common', obj)))

        if platform == 'win64':
            obj = 'fluidterm' + exeExtension[platform]
            zipObj.write(os.path.join('fluidterm', obj), os.path.join(zipDirName, platform, obj))

        EsptoolVersion = 'v4.6'

        # Put esptool and related tools in the archive
        if withEsptoolBinary[platform]:
            name = 'README-ESPTOOL.txt'
            EspRepo = 'https://github.com/espressif/esptool/releases/download/' + EsptoolVersion + '/'
            EspDir = 'esptool-' + EsptoolVersion + '-' + platform
            zipObj.write(os.path.join(sharedPath, platform, name), os.path.join(zipDirName, platform,
                         name.replace('.txt', '-' + EsptoolVersion + '.txt')))
        else:
            name = 'README-ESPTOOL-SOURCE.txt'
            EspRepo = 'https://github.com/espressif/esptool/archive/refs/tags/'
            EspDir = EsptoolVersion
            zipObj.write(os.path.join(sharedPath, 'common', name), os.path.join(zipDirName, 'common',
                         name.replace('.txt', '-' + EsptoolVersion + '.txt')))


        # Download and unzip from ESP repo
        ZipFileName = EspDir + '.zip'
        if not os.path.isfile(ZipFileName):
            with urllib.request.urlopen(EspRepo + ZipFileName) as u:
                open(ZipFileName, 'wb').write(u.read())

        if withEsptoolBinary[platform]:
            for Binary in ['esptool']:
                Binary += exeExtension[platform]
                sourceFileName = EspDir + '/' + Binary
                with ZipFile(ZipFileName, 'r') as zipReader:
                    destFileName = os.path.join(zipDirName, platform, Binary)
                    info = ZipInfo(destFileName)
                    info.external_attr = 0o100755 << 16
                    zipObj.writestr(info, zipReader.read(sourceFileName))
        else:
            zipObj.write(os.path.join(ZipFileName), os.path.join(zipDirName, 'common', 'esptool-source.zip'))

sys.exit(0)
