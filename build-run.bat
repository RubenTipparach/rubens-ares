@echo off
setlocal enabledelayedexpansion

echo ============================================
echo   ares WASM - Build ^& Run
echo   N64 Emulator in your browser (WebGPU)
echo ============================================
echo.

:: ── Configuration ──
set "WEB_DIR=%~dp0web"
set "ARES_DIR=%~dp0ares"
set "BUILD_DIR=%~dp0build"
set "PATCHES_DIR=%~dp0wasm-patches"

:: ── Step 1: Check ares submodule ──
if not exist "%ARES_DIR%\nall\GNUmakefile" (
    echo [BUILD] Initializing ares submodule...
    git submodule update --init --recursive
    if !errorlevel! neq 0 (
        echo [ERROR] Failed to initialize submodule.
        echo         Run: git submodule update --init --recursive
        pause
        exit /b 1
    )
)
echo [OK] ares submodule found

:: ── Step 2: Check for Emscripten ──
set "EMSDK_ROOT="
where emcc >nul 2>&1
if %errorlevel% neq 0 (
    echo.
    echo [INFO] emcc not found in PATH - searching for emsdk...

    if exist "%~dp0emsdk\upstream\emscripten\emcc.bat" (
        set "EMSDK_ROOT=%~dp0emsdk"
    ) else if exist "%USERPROFILE%\emsdk\upstream\emscripten\emcc.bat" (
        set "EMSDK_ROOT=%USERPROFILE%\emsdk"
    ) else if exist "C:\emsdk\upstream\emscripten\emcc.bat" (
        set "EMSDK_ROOT=C:\emsdk"
    )

    if not defined EMSDK_ROOT (
        echo [ERROR] Emscripten SDK not found.
        echo.
        echo  Install it with:
        echo    git clone https://github.com/emscripten-core/emsdk.git
        echo    cd emsdk
        echo    emsdk install latest
        echo    emsdk activate latest
        echo.
        echo  Or run install.bat to set up all dependencies.
        pause
        exit /b 1
    )

    echo [INFO] Found emsdk at !EMSDK_ROOT! - activating...
    call "!EMSDK_ROOT!\emsdk_env.bat" >nul 2>&1
    set "PATH=!EMSDK_ROOT!;!EMSDK_ROOT!\upstream\emscripten;!PATH!"
    set "EMSDK=!EMSDK_ROOT!"

    where emcc >nul 2>&1
    if !errorlevel! neq 0 (
        echo [ERROR] Still can't find emcc after activating emsdk.
        pause
        exit /b 1
    )
    echo [OK] Emscripten activated from !EMSDK_ROOT!
)
echo [OK] Emscripten found:
call emcc --version 2>&1 | findstr /i "emcc"

:: ── Step 3: Check for npm ──
where npm >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] npm not found in PATH. Install Node.js from https://nodejs.org/
    pause
    exit /b 1
)
echo [OK] npm found

:: ── Step 4: Install npm dependencies ──
cd /d "%WEB_DIR%"
if not exist "node_modules" (
    echo [BUILD] Installing npm packages...
    call npm install
)
echo [OK] npm dependencies ready

:: ── Step 5: Create build directory from ares source ──
echo.
echo ============================================
echo   Compiling ares for WebAssembly (N64 core)
echo   This may take a while on first build...
echo ============================================
echo.

cd /d "%~dp0"

:: Create build directory by copying ares source (if not exists or stale)
if not exist "%BUILD_DIR%\web-ui\GNUmakefile" (
    echo [BUILD] Copying ares source to build directory...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%" 2>nul

    :: Submodule is the wasm branch of ares — flat structure with GNUmakefiles
    :: Copy everything needed for the N64 build
    xcopy /s /y /q /i "%ARES_DIR%\ares" "%BUILD_DIR%\ares" >nul
    xcopy /s /y /q /i "%ARES_DIR%\nall" "%BUILD_DIR%\nall" >nul
    xcopy /s /y /q /i "%ARES_DIR%\libco" "%BUILD_DIR%\libco" >nul
    xcopy /s /y /q /i "%ARES_DIR%\ruby" "%BUILD_DIR%\ruby" >nul
    xcopy /s /y /q /i "%ARES_DIR%\hiro" "%BUILD_DIR%\hiro" >nul
    xcopy /s /y /q /i "%ARES_DIR%\mia" "%BUILD_DIR%\mia" >nul
    xcopy /s /y /q /i "%ARES_DIR%\thirdparty" "%BUILD_DIR%\thirdparty" >nul
    xcopy /s /y /q /i "%ARES_DIR%\scripts" "%BUILD_DIR%\scripts" >nul 2>nul

    :: Create web-ui directory
    mkdir "%BUILD_DIR%\web-ui" 2>nul

    echo [OK] ares source copied
)

:: Apply WASM patches
if exist "%PATCHES_DIR%" (
    echo [BUILD] Applying WASM patches...
    xcopy /s /y /q "%PATCHES_DIR%\*" "%BUILD_DIR%\" >nul 2>nul
    echo [OK] WASM patches applied
)

:: Copy PIF firmware ROMs
:: PIF ROMs are at ares/ares/System/Nintendo 64/ in the submodule
copy /y "%ARES_DIR%\ares\System\Nintendo 64\pif.ntsc.rom" "%BUILD_DIR%\web-ui\pif.ntsc.rom" >nul 2>nul
copy /y "%ARES_DIR%\ares\System\Nintendo 64\pif.pal.rom" "%BUILD_DIR%\web-ui\pif.pal.rom" >nul 2>nul
:: Also check one level up in case structure differs
if not exist "%BUILD_DIR%\web-ui\pif.ntsc.rom" (
    copy /y "%ARES_DIR%\System\Nintendo 64\pif.ntsc.rom" "%BUILD_DIR%\web-ui\pif.ntsc.rom" >nul 2>nul
    copy /y "%ARES_DIR%\System\Nintendo 64\pif.pal.rom" "%BUILD_DIR%\web-ui\pif.pal.rom" >nul 2>nul
)

:: ── Step 6: Build ares with Emscripten ──
cd /d "%BUILD_DIR%\web-ui"
echo [BUILD] Running: emmake make -j%NUMBER_OF_PROCESSORS% wasm32=true platform=linux
call emmake make -j%NUMBER_OF_PROCESSORS% wasm32=true platform=linux

:: Check for build output
if not exist "out\ares.wasm" (
    if not exist "out\ares" (
        echo.
        echo [ERROR] Build failed - no output files found.
        echo.
        echo  Common fixes:
        echo   - Make sure emsdk is activated: emsdk activate latest
        echo   - Try a clean build: rmdir /s /q build
        echo   - Check that GNU Make is installed ^(comes with emsdk^)
        echo.
        pause
        exit /b 1
    )
)
echo.
echo [OK] Build completed successfully!

:: ── Step 7: Deploy build artifacts ──
echo.
echo [DEPLOY] Copying build artifacts...

:: Copy JS glue (emcc may output as 'ares' without .js extension)
if exist "out\ares" if not exist "out\ares.js" (
    copy /y "out\ares" "%WEB_DIR%\ares.js" >nul
    echo   Copied ares.js
)
if exist "out\ares.js" (
    copy /y "out\ares.js" "%WEB_DIR%\" >nul
    echo   Copied ares.js
)
if exist "out\ares.wasm" (
    copy /y "out\ares.wasm" "%WEB_DIR%\" >nul
    echo   Copied ares.wasm
)
if exist "out\ares.data" (
    copy /y "out\ares.data" "%WEB_DIR%\" >nul
    echo   Copied ares.data
)

:: ── Step 8: Launch web server ──
echo.
echo ============================================
echo   Launching ares in your browser!
echo   http://localhost:3000
echo ============================================
echo.
echo   Press Ctrl+C to stop the server
echo.

cd /d "%WEB_DIR%"
call npx http-server . -p 3000 -c-1 --cors

endlocal
