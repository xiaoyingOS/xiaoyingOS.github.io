@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

REM ========================================
REM HTTPS Server Startup Script (Windows)
REM ========================================

echo ========================================
echo   HTTPS Server Startup Script
echo ========================================
echo.

REM Check Node.js installation
echo [INFO] Checking Node.js installation...
where node >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Node.js not found!
    echo [INFO] Please download and install Node.js from https://nodejs.org/
    pause
    exit /b 1
)

for /f "tokens=*" %%i in ('node -v') do set NODE_VERSION=%%i
echo [SUCCESS] Node.js installed: %NODE_VERSION%

REM Check ffmpeg installation
echo [INFO] Checking ffmpeg installation...
where ffmpeg >nul 2>&1
if %errorlevel% neq 0 (
    echo [WARNING] ffmpeg not found!
    echo [INFO] Please download and install ffmpeg from https://ffmpeg.org/download.html
    pause
    exit /b 1
)

for /f "tokens=1" %%i in ('ffmpeg -version 2^>^&1') do (
    set FFMPEG_VERSION=%%i
)
echo [SUCCESS] ffmpeg installed: %FFMPEG_VERSION%

REM Check npm installation
echo [INFO] Checking npm installation...
where npm >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] npm not found!
    pause
    exit /b 1
)

for /f "tokens=*" %%i in ('npm -v') do set NPM_VERSION=%%i
echo [SUCCESS] npm installed: %NPM_VERSION%

REM Check SSL certificates
echo [INFO] Checking SSL certificates...
if not exist "cert.pem" (
    echo [ERROR] SSL certificate file cert.pem not found!
    echo [INFO] Please ensure cert.pem and key.pem exist in current directory
    pause
    exit /b 1
)

if not exist "key.pem" (
    echo [ERROR] SSL certificate file key.pem not found!
    echo [INFO] Please ensure cert.pem and key.pem exist in current directory
    pause
    exit /b 1
)

echo [SUCCESS] SSL certificate files found

REM Check dependencies
echo [INFO] Checking project dependencies...
if not exist "node_modules\" (
    echo [INFO] Dependencies not installed, installing...
    call npm install
    if %errorlevel% neq 0 (
        echo [ERROR] Dependency installation failed!
        pause
        exit /b 1
    )
    echo [SUCCESS] Dependencies installed!
) else (
    echo [SUCCESS] Dependencies already installed, skipping.
)

REM Check if port is in use
set PORT=8691
echo [INFO] Checking if port %PORT% is in use...

for /f "tokens=5" %%a in ('netstat -ano ^| findstr ":8691"') do (
    echo [WARNING] Port %PORT% is in use
    echo [INFO] Attempting to stop the process...
    taskkill /F /PID %%a >nul 2>&1
    if !errorlevel! equ 0 (
        echo [SUCCESS] Process %%a stopped
    )
)

echo [SUCCESS] Port %PORT% is available

REM Start server
echo.
echo [INFO] Starting HTTPS server...
echo ========================================
echo.

REM Check if https-server.js exists
if not exist "https-server.js" (
    echo [ERROR] https-server.js not found!
    echo [INFO] Please ensure https-server.js exists in current directory
    pause
    exit /b 1
)

REM Start server in foreground
node https-server.js

pause