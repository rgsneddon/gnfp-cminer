@echo off
REM Community miner. Not official GNFPHash. Declared 5%% dual-login fee.
REM PE is gnfp-cminer.exe at the zip root (static OpenSSL, no extra DLL).
if exist "%~dp0..\..\gnfp-cminer.exe" (
  "%~dp0..\..\gnfp-cminer.exe" %*
  exit /b %ERRORLEVEL%
)
if exist "%~dp0gnfp-cminer.exe" (
  "%~dp0gnfp-cminer.exe" %*
  exit /b %ERRORLEVEL%
)
echo gnfp-cminer: no Windows PE next to this script.
echo Unpack gnfp-cminer-1.1.1-windows.zip from rgsneddon/gnfp-cminer v1.1.1
echo then run: gnfp-cminer.exe --selftest
echo Expected: selftest ok 986437c40fee8a876e0ca3f1e58b14fa38785a179f57f98ebbb0fb03102bd4eb
exit /b 1
