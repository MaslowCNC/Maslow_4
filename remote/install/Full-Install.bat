@echo off
rem Maslow remote: erase the board and install the remote firmware.
rem Uses tools\windows\esptool.exe, so nothing has to be installed first.
setlocal
cd /d "%~dp0"

set ESPTOOL=tools\windows\esptool.exe
if not exist "%ESPTOOL%" (
    echo Missing %ESPTOOL%. Unpack the whole zip file and run Full-Install.bat from there.
    goto fail
)

echo Maslow remote installer
echo =======================
echo.
echo Plug the remote into this computer with a USB data cable.
echo Unplug the Maslow's own USB cable first, so the installer can't pick it by mistake.
echo.
pause
echo.
echo Looking for the remote...

rem ---- Find the board -------------------------------------------------------
set LOG=%TEMP%\maslow-remote-detect.txt
"%ESPTOOL%" --after hard_reset read_mac > "%LOG%" 2>&1
set PORT=
set CHIP=
set MAC=
for /f "tokens=3" %%a in ('findstr /B /C:"Serial port " "%LOG%"') do set PORT=%%a
for /f "tokens=2,*" %%a in ('findstr /B /C:"Chip is " "%LOG%"') do set CHIP=%%b
for /f "tokens=2" %%a in ('findstr /B /C:"MAC: " "%LOG%"') do if not defined MAC set MAC=%%a
if defined PORT set PORT=%PORT::=%
if not defined PORT goto notfound
if not defined CHIP goto notfound

echo %CHIP% | findstr /B /C:"ESP32-S3" >nul
if not errorlevel 1 goto s3
echo %CHIP% | findstr /B /C:"ESP32-" >nul
if not errorlevel 1 goto esp32
echo Found %CHIP% on %PORT%, which isn't one of the remote's boards. Nothing installed.
goto fail

:s3
set ENV=es3c40p
set BOARD=4.0in ESP32-S3 display - Hosyond / lcdwiki ES3C40P
set ESPCHIP=esp32s3
goto confirm

:esp32
set ESPCHIP=esp32
echo Found an ESP32 board on %PORT%. Which display is it?
echo   1^) 2.8in Cheap Yellow Display - ESP32-2432S028R, 240x320
echo   2^) 4.0in ESP32-32E / E32R40T - 320x480
set PICK=
set /p PICK=Enter 1 or 2:
if "%PICK%"=="1" (
    set ENV=cyd
    set BOARD=2.8in Cheap Yellow Display - ESP32-2432S028R
    goto confirm
)
if "%PICK%"=="2" (
    set ENV=cyd4
    set BOARD=4.0in ESP32-32E / E32R40T
    goto confirm
)
echo Nothing installed.
goto fail

:confirm
set IMAGE=firmware\%ENV%.bin
if not exist "%IMAGE%" (
    echo Missing %IMAGE%. Unpack the whole zip file and run Full-Install.bat from there.
    goto fail
)
echo.
echo Board:  %BOARD%
echo Port:   %PORT%
echo Chip:   %CHIP%  MAC %MAC%
echo.
echo This erases everything on that board, then installs the remote firmware.
echo Afterwards the remote has to be paired with the Maslow again.
set YES=
set /p YES=Install? [y/N]
if /i not "%YES%"=="y" if /i not "%YES%"=="yes" (
    echo Nothing installed.
    goto fail
)

rem ---- Erase and install ----------------------------------------------------
echo.
echo Erasing...
"%ESPTOOL%" --chip %ESPCHIP% --port %PORT% --baud 460800 erase_flash
if errorlevel 1 (
    echo Erasing failed.
    goto fail
)
echo.
echo Installing...
"%ESPTOOL%" --chip %ESPCHIP% --port %PORT% --baud 460800 --after hard_reset write_flash 0x0 "%IMAGE%"
if errorlevel 1 (
    echo Installing failed. Try again; if it keeps failing, try another USB port or cable.
    goto fail
)

echo.
echo Installation successful!
echo The remote restarts by itself. It offers to calibrate the touch screen: touch the
echo screen within 6 seconds to do it, or wait to skip. Then pair: turn the Maslow off
echo and on again and, within 2 minutes, tap Pair on the remote.
echo.
pause
exit /b 0

:notfound
type "%LOG%"
echo.
echo No board found. Check that:
echo  - the cable carries data; some USB cables only charge
echo  - no other program, such as Arduino or a serial monitor, has the port open
echo  - the 2.8in and ESP32-32E boards need the CH340 USB driver; Windows 10 and 11
echo    usually install it by themselves the first time the board is plugged in
echo  - if it still isn't found, hold the board's BOOT button while plugging it in, then try again
goto fail

:fail
echo.
pause
exit /b 1
