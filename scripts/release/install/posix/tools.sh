#!/bin/sh
# Shared setup for the Maslow 4 USB install scripts (ESP32-S3).
# Sourced by the other scripts in this folder; run them from the folder they are in.
#
# Flash layout (firmware/max_littlefs.csv): bootloader 0x0, partitions 0x8000,
# boot_app0 0xe000, firmware 0x10000, littlefs 0x5F0000.
#
# The esptool commands use the underscore spelling (write_flash, hard_reset), which
# both esptool 4.x and 5.x accept, so an older ~/.fluidnc_venv still works.

if ! test -d ~/.fluidnc_venv; then
  if ! python3 -m venv ~/.fluidnc_venv; then
    if which apt >/dev/null 2>&1; then
      sudo apt install python3-venv
      if ! python3 -m venv ~/.fluidnc_venv; then
        echo Unable to create a Python virtual environment
        return 1
      fi
    else
      echo The Python venv module is not present on your system
      echo and we were not able to install it automatically
      return 1
    fi
  fi
fi
. ~/.fluidnc_venv/bin/activate

if ! python3 -c "import serial, xmodem" >/dev/null 2>&1; then
  if ! python3 -m pip install xmodem pyserial; then
    echo Installation of xmodem and pyserial modules failed
    deactivate
    return 1
  fi
fi

if ! which esptool.py >/dev/null 2>&1; then
  echo esptool.py not found, attempting to install
  if ! python3 -m pip install -q common/esptool-source.zip; then
    echo Installation of esptool.py failed
    deactivate
    return 1
  fi
fi

EsptoolPath=esptool.py
BaseArgs="--chip esp32s3 --baud 921600"
SetupArgs="--before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect"

esptool_basic () {
    echo $EsptoolPath $BaseArgs $*
    if ! $EsptoolPath $BaseArgs $*; then
        echo esptool failed
        deactivate
        exit 1
    fi
}

esptool_write () {
    esptool_basic $SetupArgs $*
}

esptool_erase () {
    esptool_basic erase_flash
}

BuildType=wifi_s3
Bootloader="0x0000 ${BuildType}/bootloader.bin"
Bootapp="0xe000 common/boot_app0.bin"
Firmware="0x10000 ${BuildType}/firmware.bin"
Partitions="0x8000 ${BuildType}/partitions.bin"
LocalFS="0x5F0000 ${BuildType}/littlefs.bin"

install_firmware () {
    esptool_write $Bootloader $Bootapp $Firmware $Partitions
}

install_filesystem () {
    esptool_write $LocalFS
}

run_fluidterm () {
    python3 common/fluidterm.py $*
}
