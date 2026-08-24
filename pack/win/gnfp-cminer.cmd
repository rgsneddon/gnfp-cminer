@echo off
REM Official gnfp-cminer. Declared 5%% dual-login miner fee.
REM PE is gnfp-cminer.exe at the zip root (v0.4 OpenSSL DLLs sit next to it).
if exist "%~dp0..\..\gnfp-cminer.exe" (
  "%~dp0..\..\gnfp-cminer.exe" %*
  exit /b %ERRORLEVEL%
)
if exist "%~dp0gnfp-cminer.exe" (
  "%~dp0gnfp-cminer.exe" %*
  exit /b %ERRORLEVEL%
)
echo gnfp-cminer: no Windows PE next to this script.
echo Unpack gnfp-cminer-1.1.4-windows.zip from rgsneddon/gnfp-cminer v1.1.4
echo then run: gnfp-cminer.exe --selftest
echo or double-click example.bat after editing the gnfp1 address.
echo Expected: selftest ok 986437c40fee8a876e0ca3f1e58b14fa38785a179f57f98ebbb0fb03102bd4eb
exit /b 1
