@echo off
:: ─────────────────────────────────────────────────────────────
::  deploy-client.bat  —  Build and install NetCommand Client
::                         as a Windows Service
::
::  Run as Administrator:
::    deploy-client.bat <server-ip> [port]
::
::  Requirements:
::    - MinGW-w64 (g++) in PATH  OR  MSVC (cl.exe) in PATH
::    - curl (built into Windows 10+)
:: ─────────────────────────────────────────────────────────────
setlocal EnableDelayedExpansion

set SERVER_IP=%1
set PORT=%2
if "%SERVER_IP%"=="" (
    echo Usage: deploy-client.bat ^<server-ip^> [port]
    exit /b 1
)
if "%PORT%"=="" set PORT=7890

echo ========================================
echo  NetCommand Client ^| Windows Deploy
echo  Server: %SERVER_IP%:%PORT%
echo ========================================

:: Check admin
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [ERROR] Please run as Administrator.
    pause
    exit /b 1
)

set SCRIPT_DIR=%~dp0
set BINARY=netcommand-client.exe
set INSTALL_DIR=%ProgramFiles%\NetCommand
set INSTALL_PATH=%INSTALL_DIR%\%BINARY%

:: ── 1. Download stb_image_write.h ────────────────────────
echo [1/4] Fetching stb_image_write.h...
if not exist "%SCRIPT_DIR%stb_image_write.h" (
    curl -sL "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h" ^
         -o "%SCRIPT_DIR%stb_image_write.h"
    if errorlevel 1 (
        echo [ERROR] Download failed. Check internet connection.
        exit /b 1
    )
    echo   Downloaded.
) else (
    echo   Already present.
)

:: ── 2. Detect compiler and build ─────────────────────────
echo [2/4] Building...

where g++ >nul 2>&1
if %errorLevel%==0 (
    echo   Using MinGW g++...
    g++ -std=c++17 -O2 -mwindows ^
        -I"%SCRIPT_DIR%" -I"%SCRIPT_DIR%..\common" ^
        "%SCRIPT_DIR%main.cpp" ^
        "%SCRIPT_DIR%screencapture.cpp" ^
        "%SCRIPT_DIR%inputinjector.cpp" ^
        -o "%SCRIPT_DIR%%BINARY%" ^
        -lws2_32 -lgdi32 -lpthread
) else (
    where cl >nul 2>&1
    if %errorLevel%==0 (
        echo   Using MSVC cl.exe...
        cl /std:c++17 /O2 /EHsc ^
           /I"%SCRIPT_DIR%" /I"%SCRIPT_DIR%..\common" ^
           "%SCRIPT_DIR%main.cpp" ^
           "%SCRIPT_DIR%screencapture.cpp" ^
           "%SCRIPT_DIR%inputinjector.cpp" ^
           /Fe:"%SCRIPT_DIR%%BINARY%" ^
           /link ws2_32.lib gdi32.lib
    ) else (
        echo [ERROR] No compiler found. Install MinGW-w64 or MSVC.
        exit /b 1
    )
)

if not exist "%SCRIPT_DIR%%BINARY%" (
    echo [ERROR] Build failed.
    exit /b 1
)
echo   Build successful.

:: ── 3. Install binary ─────────────────────────────────────
echo [3/4] Installing to %INSTALL_DIR%...
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"
copy /y "%SCRIPT_DIR%%BINARY%" "%INSTALL_PATH%" >nul
echo   Copied.

:: ── 4. Register as Windows Service ───────────────────────
echo [4/4] Registering Windows Service...
"%INSTALL_PATH%" --install %SERVER_IP% %PORT%

echo.
echo ========================================
echo  Done! NetCommand Client is running.
echo  Server: %SERVER_IP%:%PORT%
echo.
echo  Manage service:
echo    sc query NetCommandClient
echo    sc stop  NetCommandClient
echo    sc start NetCommandClient
echo.
echo  Uninstall:
echo    "%INSTALL_PATH%" --uninstall
echo    rmdir /s /q "%INSTALL_DIR%"
echo ========================================
pause
