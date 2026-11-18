@echo off
rem Replace the filesystem with the release's (web UI and config templates).
rem This deletes maslow.yaml and any uploaded files on the machine.
cd /d "%~dp0"
call tools.bat

echo %EsptoolPath% %BaseArgs% %SetupArgs% %LocalFS%
%EsptoolPath% %BaseArgs% %SetupArgs% %LocalFS%
if errorlevel 1 (
   echo esptool failed
   exit /b 1
)
exit /b 0
