@echo off
setlocal enabledelayedexpansion

echo ============================================
echo   rubens-ares - Dependency Installer
echo ============================================
echo.

:: ── Check for Git ──
where git >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Git is not installed. Download from https://git-scm.com/downloads
    pause
    exit /b 1
)
echo [OK] Git found

:: ── Initialize submodule ──
if not exist "%~dp0ares\ares\ares.hpp" (
    echo [INSTALL] Initializing ares submodule...
    git submodule update --init --recursive
    if %errorlevel% neq 0 (
        echo [ERROR] Failed to initialize submodule.
        pause
        exit /b 1
    )
)
echo [OK] ares submodule ready

:: ── Check for Node.js / npm ──
where node >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Node.js not found. Install from https://nodejs.org/
    pause
    exit /b 1
)
echo [OK] Node.js found:
node --version

:: ── Install npm dependencies ──
echo [INSTALL] Installing npm packages...
cd /d "%~dp0web"
call npm install
echo [OK] npm packages installed
cd /d "%~dp0"

:: ── Install Emscripten SDK ──
set "EMSDK_DIR=%~dp0emsdk"

where emcc >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] Emscripten already available in PATH
    goto :emsdk_done
)

if exist "%EMSDK_DIR%\emsdk.bat" (
    echo [OK] emsdk already cloned
    goto :emsdk_activate
)

echo [INSTALL] Cloning Emscripten SDK...
git clone https://github.com/emscripten-core/emsdk.git "%EMSDK_DIR%"
if %errorlevel% neq 0 (
    echo [ERROR] Failed to clone emsdk
    pause
    exit /b 1
)

:emsdk_activate
echo [INSTALL] Installing latest Emscripten toolchain...
echo          This downloads ~1 GB - may take a few minutes.
echo.

cd /d "%EMSDK_DIR%"
call emsdk.bat install latest
call emsdk.bat activate latest
call emsdk_env.bat >nul 2>&1

where emcc >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] emcc still not found after activation.
    pause
    exit /b 1
)
echo [OK] Emscripten installed

:emsdk_done
cd /d "%~dp0"

echo.
echo ============================================
echo   Installation Complete!
echo ============================================
echo.
echo   Next step: run build-run.bat
echo.

endlocal
pause
