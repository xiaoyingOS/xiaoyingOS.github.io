@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

title HTTPS Server Startup

echo ========================================
echo     HTTPS Server One-Click Start
echo ========================================
echo.

:: Check if Node.js is installed
where node >nul 2>nul
if %errorlevel% neq 0 (
    echo [Error] Node.js not detected
    echo [Tip] Please install Node.js first
    pause
    exit /b 1
)

:: Get script directory
set "SCRIPT_DIR=%~dp0"

:: Check if server file exists
set "SERVER_FILE=%SCRIPT_DIR%server-https.js"
if not exist "%SERVER_FILE%" (
    echo [Error] Server file not found: %SERVER_FILE%
    pause
    exit /b 1
)

:: Check if SSL certificate exists
set "CERT_FILE=%SCRIPT_DIR%cert.pem"
set "KEY_FILE=%SCRIPT_DIR%key.pem"

if not exist "%CERT_FILE%" if not exist "%KEY_FILE%" (
    echo [Warning] SSL certificate files not found
    echo [Tip] Generating self-signed certificate...

    :: Generate self-signed certificate
    openssl req -x509 -newkey rsa:2048 -keyout "%KEY_FILE%" -out "%CERT_FILE%" -days 365 -nodes -subj "/C=CN/ST=Beijing/L=Beijing/O=WeQTe/OU=Development/CN=localhost" 2>nul

    if !errorlevel! equ 0 (
        echo [Success] SSL certificate generated successfully
    ) else (
        echo [Error] SSL certificate generation failed
        pause
        exit /b 1
    )
)

:: Check if server process is already running
echo [Check] Checking for running server...
for /f "tokens=2" %%a in ('tasklist /fi "imagename eq node.exe" /fo csv ^| find "node.exe"') do (
    set "PID=%%~a"
    wmic process where "processid=!PID!" get commandline 2>nul | find "server-https.js" >nul
    if !errorlevel! equ 0 (
        echo [Found] Found running server process (PID: !PID!)
        echo [Stop] Stopping old server...
        taskkill /f /pid !PID! >nul 2>nul
        echo [Success] Old server stopped
    )
)

:: Start server
echo.
echo [Start] Starting HTTPS server...
echo.

cd /d "%SCRIPT_DIR%"

:: Start server (run in background with logging)
start /min "HTTPS Server" cmd /c "node server-https.js > https-server.log 2>&1"

:: Wait for server to start
timeout /t 2 /nobreak >nul

echo ========================================
echo [Success] HTTPS server started successfully!
echo ========================================
echo.
echo Server Info:
echo   Log file: %SCRIPT_DIR%https-server.log
echo.
echo Server Output:
echo ----------------------------------------
type %SCRIPT_DIR%https-server.log
echo ----------------------------------------
echo.
echo Tips:
echo   * Browser will warn about insecure certificate, click Advanced then Continue
echo   * View logs: type %SCRIPT_DIR%https-server.log
echo.
echo ========================================
echo.
echo Press any key to close this window (server will keep running)...
pause >nul