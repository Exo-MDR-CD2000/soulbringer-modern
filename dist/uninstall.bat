@echo off
setlocal EnableExtensions
title Soulbringer Modern - uninstall
cd /d "%~dp0"

echo Soulbringer Modern uninstaller
echo.
call :restore "."
call :restore "data\Stealth\Render"
if exist "data\Stealth\Render\SoulbringerModern.ini" del "data\Stealth\Render\SoulbringerModern.ini"
echo.
echo Done. The original renderer is restored.
echo You can delete SoulbringerModern.ini, the SoulbringerModern folder,
echo install.bat and uninstall.bat yourself.
echo.
pause
exit /b 0

:restore
set "DIR=%~1"
if not exist "%DIR%\D3d7Lib_orig.dll" exit /b 0
if exist "%DIR%\D3d7Lib.dll" del "%DIR%\D3d7Lib.dll"
ren "%DIR%\D3d7Lib_orig.dll" "D3d7Lib.dll" && echo   %DIR%\D3d7Lib.dll restored
exit /b 0
