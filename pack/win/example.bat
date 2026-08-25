@echo off
REM Example launch for gnfp-cminer 0.5 (Windows).
REM 1) Replace gnfp1YOURADDRESS with your payout address.
REM 2) Change .worker to a unique name per box.
REM 3) Set --threads to this machine's logical CPUs (no 256 farm cap).
REM TLS to de.restoreprivacy.online:1474 is the default.

cd /d "%~dp0"

if not exist "gnfp-cminer.exe" (
  echo gnfp-cminer.exe missing. Unpack gnfp-cminer-0.5-windows.zip first.
  pause
  exit /b 1
)

REM Edit this line, then double-click this file (or run it from cmd):
gnfp-cminer.exe --user gnfp1YOURADDRESS.worker --threads 8

pause
