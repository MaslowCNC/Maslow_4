@echo off
rem Shared settings for the Maslow 4 USB install scripts (ESP32-S3).
rem Flash layout (firmware/max_littlefs.csv): bootloader 0x0, partitions 0x8000,
rem boot_app0 0xe000, firmware 0x10000, littlefs 0x5F0000.
rem The esptool commands use the underscore spelling, which esptool 4.x and 5.x both accept.

set EsptoolPath=win64\esptool.exe
set BaseArgs=--chip esp32s3 --baud 921600
set SetupArgs=--before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect

set BuildType=wifi_s3
set Bootloader=0x0000 %BuildType%\bootloader.bin
set Bootapp=0xe000 common\boot_app0.bin
set Firmware=0x10000 %BuildType%\firmware.bin
set Partitions=0x8000 %BuildType%\partitions.bin
set LocalFS=0x5F0000 %BuildType%\littlefs.bin
