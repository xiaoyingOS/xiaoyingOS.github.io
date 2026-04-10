@echo off
setlocal enabledelayedexpansion

echo ========================================
echo Cross-platform System Monitor - Build
echo ========================================
echo.

set PROJECT_DIR=%~dp0
set BUILD_DIR=%PROJECT_DIR%build
set BUILD_TYPE=Release
set COMPILER=gcc

if not "%~1"=="" (
    if /i "%~1"=="debug" set BUILD_TYPE=Debug
    if /i "%~1"=="release" set BUILD_TYPE=Release
    if /i "%~1"=="gcc" set COMPILER=gcc
    if /i "%~1"=="msvc" set COMPILER=msvc
    if /i "%~1"=="clean" goto clean
    if /i "%~1"=="help" goto help
)

echo Build Type: %BUILD_TYPE%
echo Compiler: %COMPILER%
echo.

where cmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: CMake not found
    exit /b 1
)

echo CMake found
cmake --version
echo.

if /i "%COMPILER%"=="gcc" (
    echo Using MinGW/GCC
    where gcc >nul 2>&1
    if errorlevel 1 (
        echo GCC not found, switching to MSVC
        set COMPILER=msvc
    ) else (
        set GENERATOR="MinGW Makefiles"
    )
)

if /i "%COMPILER%"=="msvc" (
    echo Using MSVC
    if exist "D:\VS2019\Common7\IDE\devenv.exe" (
        set GENERATOR="Visual Studio 16 2019"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022" (
        set GENERATOR="Visual Studio 17 2022"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019" (
        set GENERATOR="Visual Studio 16 2019"
    ) else (
        echo ERROR: Visual Studio not found
        echo Please enter VS path:
        set /p VS_PATH=
        if exist "!VS_PATH!\Common7\IDE\devenv.exe" (
            set GENERATOR="Visual Studio 16 2019"
        ) else (
            echo ERROR: Invalid VS path
            exit /b 1
        )
    )
)

echo Generator: %GENERATOR%
echo.

echo Detecting GPU...
set HAS_NVIDIA=0
set HAS_AMD=0
set CUDA_PATH=

wmic path win32_VideoController get name 2>nul | findstr /i nvidia >nul
if not errorlevel 1 set HAS_NVIDIA=1

wmic path win32_VideoController get name 2>nul | findstr /i amd >nul
if not errorlevel 1 set HAS_AMD=1

if %HAS_NVIDIA%==1 echo NVIDIA GPU detected
if %HAS_AMD%==1 echo AMD GPU detected

if %HAS_NVIDIA%==1 (
    echo Searching CUDA Toolkit...
    if exist "D:\NVIDIA\CUDA_Toolkit_NEED\include\nvml.h" (
        set CUDA_PATH=D:\NVIDIA\CUDA_Toolkit_NEED
        echo CUDA found: !CUDA_PATH!
    ) else (
        echo CUDA not found at D:\NVIDIA\CUDA_Toolkit_NEED
        echo Please enter CUDA path:
        set /p CUDA_INPUT=
        if exist "!CUDA_INPUT!\include\nvml.h" (
            set CUDA_PATH=!CUDA_INPUT!
        ) else (
            set CUDA_PATH=
        )
    )
)

echo.

if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
mkdir "%BUILD_DIR%"
cd "%BUILD_DIR%"

echo Step 1: CMake configure
set CMAKE_ARGS=-G %GENERATOR% -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if defined CUDA_PATH (
    set CMAKE_ARGS=!CMAKE_ARGS! -DNVML_INCLUDE_DIR=!CUDA_PATH!\include -DNVML_LIBRARY=!CUDA_PATH!\lib\x64\nvml.lib
)
cmake !CMAKE_ARGS! %PROJECT_DIR%
if errorlevel 1 (
    echo CMake failed
    cd %PROJECT_DIR%
    exit /b 1
)

echo.
echo Step 2: Building
cmake --build . --config %BUILD_TYPE%
if errorlevel 1 (
    echo Build failed
    cd %PROJECT_DIR%
    exit /b 1
)

echo.
echo Step 3: Done
echo.
echo ========================================
echo Build Success!
echo ========================================
echo.

if /i "%COMPILER%"=="gcc" (
    echo Executable: %BUILD_DIR%\system_monitor_test.exe
) else (
    echo Executable: %BUILD_DIR%\%BUILD_TYPE%\system_monitor_test.exe
)

cd %PROJECT_DIR%
goto end

:clean
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
echo Build directory cleaned
cd %PROJECT_DIR%
goto end

:help
echo Usage: build_win.bat [options]
echo.
echo Options:
echo   debug        Debug build
echo   release      Release build
echo   gcc          Use GCC
echo   msvc         Use MSVC
echo   clean        Clean build
echo   help         Show help
echo.

:end
endlocal