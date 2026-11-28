@echo off
setlocal EnableExtensions EnableDelayedExpansion
title Soulbringer Modern - install
cd /d "%~dp0"

echo Soulbringer Modern installer
echo.

rem ---- must be run from the game folder (GOG or Steam release)
if not exist "SoulbringeVCnoeax.exe" if not exist "SoulbringeVC.exe" (
    echo ERROR: Soulbringer was not found in this folder:
    echo   %CD%
    echo Extract the Soulbringer Modern zip into the folder that contains
    echo SoulbringeVC.exe / SoulbringeVCnoeax.exe and run install.bat from there.
    goto :fail
)
if not exist "SoulbringerModern\D3d7Lib.dll" (
    echo ERROR: SoulbringerModern\D3d7Lib.dll is missing. Extract the whole zip.
    goto :fail
)

set INSTALLED=0
call :install_into "."
call :install_into "data\Stealth\Render"

if "%INSTALLED%"=="0" (
    echo ERROR: no D3d7Lib.dll renderer was found in the game folder.
    goto :fail
)

if not exist "SoulbringerModern.ini" (
    echo NOTE: SoulbringerModern.ini not found, defaults will be used.
)

echo.
echo Done. Start the game as usual.
echo To remove Soulbringer Modern, run uninstall.bat.
echo.
pause
exit /b 0

:install_into
set "DIR=%~1"
if not exist "%DIR%\D3d7Lib.dll" if not exist "%DIR%\D3d7Lib_orig.dll" exit /b 0

if not exist "%DIR%\D3d7Lib_orig.dll" (
    rem first install: the current D3d7Lib.dll must be the original renderer
    for %%F in ("%DIR%\D3d7Lib.dll") do set SIZE=%%~zF
    if not "!SIZE!"=="151552" (
        echo WARNING: %DIR%\D3d7Lib.dll is not the expected original renderer
        echo          ^(size !SIZE! bytes, expected 151552^). Skipping this folder.
        exit /b 0
    )
    ren "%DIR%\D3d7Lib.dll" "D3d7Lib_orig.dll" || (echo ERROR: could not rename %DIR%\D3d7Lib.dll & exit /b 0)
    echo   %DIR%\D3d7Lib.dll  -^>  D3d7Lib_orig.dll  ^(original kept^)
) else (
    echo   %DIR%: original already backed up, updating
)
copy /y "SoulbringerModern\D3d7Lib.dll" "%DIR%\D3d7Lib.dll" >nul || (echo ERROR: could not copy into %DIR% & exit /b 0)
if exist "SoulbringerModern.ini" if not "%DIR%"=="." (
    if not exist "%DIR%\SoulbringerModern.ini" copy /y "SoulbringerModern.ini" "%DIR%\SoulbringerModern.ini" >nul
)
echo   %DIR%\D3d7Lib.dll  installed
set INSTALLED=1
exit /b 0

:fail
echo.
echo Installation did not complete. Nothing was changed unless listed above.
pause
exit /b 1
