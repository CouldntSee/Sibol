@echo off
rem ============================================================
rem  SIBOL launcher - no install, no admin, no internet.
rem  Keep this beside SIBOL.html and double-click.
rem  Use this if your PC blocks .vbs scripts.
rem ============================================================
setlocal enabledelayedexpansion
title SIBOL

set "HTML=%~dp0SIBOL.html"
if not exist "%HTML%" set "HTML=%~dp0index.html"
if not exist "%HTML%" (
  echo.
  echo  SIBOL.html was not found next to this launcher.
  echo  Put SIBOL.html in:  %~dp0
  echo.
  pause
  exit /b 1
)

set "URL=file:///%HTML:\=/%"
set "URL=!URL: =%%20!"

set "BROWSER="
for %%P in (
  "%ProgramFiles%\Google\Chrome\Application\chrome.exe"
  "%ProgramFiles(x86)%\Google\Chrome\Application\chrome.exe"
  "%LOCALAPPDATA%\Google\Chrome\Application\chrome.exe"
  "%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe"
  "%ProgramFiles%\Microsoft\Edge\Application\msedge.exe"
) do (
  if not defined BROWSER if exist %%P set "BROWSER=%%~P"
)

echo.
echo  SIBOL - offline node app
echo  ----------------------------------------
echo  1. Power the node.
echo  2. Join Wi-Fi  SIBOL-NODE  (no password).
echo  3. Enter the IP and CODE from the OLED.
echo.

if defined BROWSER (
  start "" "!BROWSER!" --app="!URL!" --window-size=1480,940 --no-first-run --no-default-browser-check
) else (
  echo  Chrome/Edge not found - opening in the default browser.
  start "" "%HTML%"
)

timeout /t 2 >nul
endlocal