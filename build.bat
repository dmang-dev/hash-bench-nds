@echo off
REM Build wrapper for hash-bench-nds (devkitARM + libnds + ndstool).
REM
REM Mirrors hash-bench-gba\build.bat: forces Windows-style devkitPro paths
REM (in case PowerShell inherits msys-style values), then invokes make.
REM ndstool is part of devkitPro tools and is on PATH after the env setup
REM below.

setlocal enableextensions enabledelayedexpansion

set DEVKITPRO=C:/devkitPro
set DEVKITARM=C:/devkitPro/devkitARM
set PATH=C:\devkitPro\msys2\usr\bin;C:\devkitPro\devkitARM\bin;C:\devkitPro\tools\bin;%PATH%

if not exist "C:\devkitPro\devkitARM\bin\arm-none-eabi-gcc.exe" (
    echo ERROR: devkitARM not found under C:\devkitPro\devkitARM.
    echo Install devkitPro with the nds-dev meta-package.
    exit /b 1
)
if not exist "C:\devkitPro\tools\bin\ndstool.exe" (
    echo ERROR: ndstool.exe not found - install devkitPro nds-dev.
    exit /b 1
)

cd /d "%~dp0"

if "%1"=="clean" (
    make clean 2>nul
    exit /b 0
)

set OUTNAME=hash-bench-nds.nds

make 2>nul

if not exist "%OUTNAME%" (
    echo.
    echo Build FAILED - no .nds produced.
    exit /b 1
)

if not exist "%~dp0artifacts" mkdir "%~dp0artifacts"
copy /Y "%OUTNAME%" "%~dp0artifacts\%OUTNAME%" >nul

echo.
echo Build OK. Fresh ROM at:
echo     %~dp0%OUTNAME%
echo     %~dp0artifacts\%OUTNAME%

endlocal
