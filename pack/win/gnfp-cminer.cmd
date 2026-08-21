@echo off
REM Community miner. Not official GNFPHash.
REM This Mac does not ship a PE. Build with MinGW + OpenSSL + pthreads, or wait for a tested windows pack.
if exist "%~dp0gnfp-cminer.exe" (
  "%~dp0gnfp-cminer.exe" %*
  exit /b %ERRORLEVEL%
)
if exist "%~dp0..\..\gnfp-cminer.exe" (
  "%~dp0..\..\gnfp-cminer.exe" %*
  exit /b %ERRORLEVEL%
)
echo gnfp-cminer: no Windows PE in this tree yet.
echo Build on Windows with OpenSSL and pthreads, or use the Darwin/Linux binary from a tested pack.
echo This is not the official GNFPHash miner. Declared 5%% dual-login fee.
exit /b 1
