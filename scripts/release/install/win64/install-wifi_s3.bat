@echo off
rem Install the Maslow 4 firmware. Keeps the filesystem (maslow.yaml, web UI, files).
cd /d "%~dp0"
call tools.bat

echo %EsptoolPath% %BaseArgs% %SetupArgs% %Bootloader% %Bootapp% %Firmware% %Partitions%
%EsptoolPath% %BaseArgs% %SetupArgs% %Bootloader% %Bootapp% %Firmware% %Partitions%
if errorlevel 1 (
   echo esptool failed
   exit /b 1
)
exit /b 0
