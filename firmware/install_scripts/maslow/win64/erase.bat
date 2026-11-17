@echo off
rem Erase the whole flash, including saved settings, calibration and belt lengths.
cd /d "%~dp0"
call tools.bat

echo %EsptoolPath% %BaseArgs% erase_flash
%EsptoolPath% %BaseArgs% erase_flash
if errorlevel 1 (
   echo esptool failed
   exit /b 1
)
exit /b 0
