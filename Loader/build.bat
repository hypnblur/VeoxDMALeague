@echo off
setlocal

:: Find VS install — try VS 2025 (v18) first, then VS 2022
set "VSBASE="
if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VSBASE=C:\Program Files\Microsoft Visual Studio\18\Community"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VSBASE=C:\Program Files\Microsoft Visual Studio\2022\Community"
) else (
    echo ERROR: Visual Studio not found.
    exit /b 1
)

echo Using: %VSBASE%
call "%VSBASE%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

echo Building PureLoader.exe ...
cl /O2 /EHsc /std:c++20 ^
    /D_CRT_SECURE_NO_WARNINGS ^
    /Fe:PureLoader.exe ^
    LoaderMain.cpp ^
    /link ^
    bcrypt.lib crypt32.lib winhttp.lib ^
    advapi32.lib user32.lib shell32.lib ^
    psapi.lib

if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)

echo.
echo Build OK: PureLoader.exe
echo.
echo BEFORE FIRST RUN:
echo   1. Edit KeyAuthAPI.h — set g_ownerID and g_appName to your KeyAuth dashboard values
echo   2. Place LeagueDMA.exe in the same folder as PureLoader.exe
echo   3. Run: PureLoader.exe [optional-license-key]
