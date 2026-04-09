@echo off
setlocal
set "ROOT=%~dp0"
set "PATH=%ROOT%emsdk;%ROOT%emsdk\upstream\emscripten;%PATH%"
set "EMSDK=%ROOT%emsdk"

:: Copy patches to build dir
xcopy /s /y /q "%ROOT%wasm-patches\*" "%ROOT%build\" >nul 2>nul

:: Build
cd /d "%ROOT%build\web-ui"
emmake make -j%NUMBER_OF_PROCESSORS% wasm32=true platform=linux
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

:: Deploy to web dir
if exist "out\ares" copy /y "out\ares" "%ROOT%web\ares.js" >nul
if exist "out\ares.wasm" copy /y "out\ares.wasm" "%ROOT%web\" >nul
if exist "out\ares.data" copy /y "out\ares.data" "%ROOT%web\" >nul

echo [OK] Build deployed to web/
endlocal
