@echo off
REM Build MakcuCLI standalone tool
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl.exe /nologo /O2 /EHsc /std:c++20 /D_CRT_SECURE_NO_WARNINGS /Fe:MakcuCLI.exe MakcuCLI.cpp setupapi.lib user32.lib
if %errorlevel%==0 (
    echo Build OK: MakcuCLI.exe
) else (
    echo Build FAILED
)
