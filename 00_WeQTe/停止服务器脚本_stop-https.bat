@echo off
chcp 65001 >nul

title HTTPS Server Stop

echo ========================================
echo         Stop HTTPS Server
echo ========================================
echo.

echo [Check] Checking for HTTPS server on port 8443...
echo.

for /f "tokens=5" %%a in ('netstat -ano ^| findstr ":8443.*LISTENING"') do (
    set "PID=%%a"
    goto :found
)

echo [Info] No HTTPS server found running on port 8443
goto :end

:found
echo [Found] Server process found (PID: %PID%)
echo [Stop] Stopping server...

taskkill /f /pid %PID% >nul 2>nul

if %errorlevel% equ 0 (
    echo [Success] HTTPS server stopped successfully (PID: %PID%)
) else (
    echo [Error] Failed to stop server process (PID: %PID%)
)

:end
echo.
echo ========================================
echo.
pause