#!/bin/sh
# Maslow remote: erase the board and install the remote firmware.
# Uses the esptool program in tools/, so nothing has to be installed first.
# macOS: double-click Full-Install.command.  Linux: sh Full-Install.sh

cd "$(dirname "$0")" || exit 1

finish() {
    echo
    printf 'Press Enter to close...'
    read -r _
    exit "$1"
}

# ---- Pick the esptool for this computer ------------------------------------
os=$(uname -s)
arch=$(uname -m)
case "$os-$arch" in
    Darwin-arm64) esptool=tools/macos-arm64/esptool ;;
    Darwin-x86_64) esptool=tools/macos-amd64/esptool ;;
    Linux-x86_64) esptool=tools/linux-amd64/esptool ;;
    Linux-aarch64 | Linux-arm64) esptool=tools/linux-aarch64/esptool ;;
    *) esptool= ;;
esac
if [ -n "$esptool" ] && [ -f "$esptool" ]; then
    chmod +x "$esptool" 2>/dev/null
    # A downloaded zip marks its files as quarantined, which stops macOS running esptool.
    [ "$os" = Darwin ] && xattr -dr com.apple.quarantine tools 2>/dev/null
elif command -v esptool >/dev/null 2>&1; then
    esptool=esptool
elif command -v esptool.py >/dev/null 2>&1; then
    esptool=esptool.py
else
    echo "This package has no esptool for $os $arch."
    echo "Download the package for your computer, or use the browser installer:"
    echo "  https://documentation.maslowcnc.com/remote-installer/"
    finish 1
fi

echo "Maslow remote installer"
echo "======================="
echo
echo "Plug the remote into this computer with a USB data cable."
echo "Unplug the Maslow's own USB cable first, so the installer can't pick it by mistake."
echo
printf 'Press Enter when the remote is plugged in...'
read -r _
echo
echo "Looking for the remote..."

# ---- Find the board ---------------------------------------------------------
log=$(mktemp 2>/dev/null || echo /tmp/maslow-remote-detect.txt)
"$esptool" --after hard_reset read_mac >"$log" 2>&1
status=$?
port=$(grep '^Serial port ' "$log" | tail -1 | awk '{print $3}' | tr -d ':')
chip=$(grep '^Chip is ' "$log" | head -1 | sed 's/^Chip is //')
mac=$(grep '^MAC: ' "$log" | head -1 | sed 's/^MAC: //')
if [ $status -ne 0 ] || [ -z "$port" ] || [ -z "$chip" ]; then
    tail -5 "$log"
    rm -f "$log"
    echo
    echo "No board found.  Check that:"
    echo " - the cable carries data (some USB cables only charge)"
    echo " - no other program (Arduino, a serial monitor) has the port open"
    [ "$os" = Linux ] && echo " - you can use serial ports: sudo usermod -aG dialout \$USER, then log out and in"
    echo " - if it still isn't found, hold the board's BOOT button while plugging it in, then try again"
    finish 1
fi
rm -f "$log"

case "$chip" in
    ESP32-S3*)
        env=es3c40p
        board='4.0" ESP32-S3 display (Hosyond / lcdwiki ES3C40P)'
        espchip=esp32s3
        ;;
    ESP32-D0* | ESP32-U4* | ESP32-PICO* | "ESP32 "*)
        espchip=esp32
        echo "Found an ESP32 board on $port.  Which display is it?"
        echo "  1) 2.8\" Cheap Yellow Display (ESP32-2432S028R, 240x320)"
        echo "  2) 4.0\" ESP32-32E / E32R40T (320x480)"
        printf 'Enter 1 or 2: '
        read -r pick
        case "$pick" in
            1) env=cyd; board='2.8" Cheap Yellow Display (ESP32-2432S028R)' ;;
            2) env=cyd4; board='4.0" ESP32-32E / E32R40T' ;;
            *) echo "Nothing installed."; finish 1 ;;
        esac
        ;;
    *)
        echo "Found $chip on $port, which isn't one of the remote's boards.  Nothing installed."
        finish 1
        ;;
esac

image="firmware/$env.bin"
if [ ! -f "$image" ]; then
    echo "Missing $image.  Unpack the whole zip file and run the installer from there."
    finish 1
fi

echo
echo "Board:  $board"
echo "Port:   $port"
echo "Chip:   $chip  (MAC $mac)"
echo
echo "This erases everything on that board, then installs the remote firmware."
echo "Afterwards the remote has to be paired with the Maslow again."
printf 'Install? [y/N] '
read -r yes
case "$yes" in
    y | Y | yes | YES) ;;
    *) echo "Nothing installed."; finish 1 ;;
esac

# ---- Erase and install ------------------------------------------------------
echo
echo "Erasing..."
if ! "$esptool" --chip "$espchip" --port "$port" --baud 460800 erase_flash; then
    echo "Erasing failed."
    finish 1
fi
echo
echo "Installing..."
if ! "$esptool" --chip "$espchip" --port "$port" --baud 460800 --after hard_reset write_flash 0x0 "$image"; then
    echo "Installing failed.  Try again; if it keeps failing, try another USB port or cable."
    finish 1
fi

echo
echo "Installation successful!"
echo "The remote restarts by itself.  It offers to calibrate the touch screen: touch the"
echo "screen within 6 seconds to do it, or wait to skip.  Then pair: turn the Maslow off"
echo "and on again and, within 2 minutes, tap Pair on the remote."
finish 0
