@echo off
rem Fresh install: erase the flash, then write the filesystem and the firmware.
rem This deletes maslow.yaml, saved settings and calibration.
cd /d "%~dp0"

echo *  ------ Erasing flash  *
call erase.bat
if errorlevel 1 (
   echo *  Erasing flash FAILED!  *
   pause
   exit /b 1
)

echo *  ------ Installing filesystem  *
call install-fs.bat
if errorlevel 1 (
   echo *  Installing filesystem FAILED!  *
   pause
   exit /b 1
)

echo *  ------ Installing firmware  *
call install-wifi_s3.bat
if errorlevel 1 (
   echo *  Installing firmware FAILED!  *
   pause
   exit /b 1
)

echo *  ------ Done  *
echo Starting fluidterm
win64\fluidterm.exe

pause
