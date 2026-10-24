@echo off
rem cli_smoke.bat - end-to-end smoke test of DPKViewer.exe command line mode.
rem
rem usage: tests\tools\cli_smoke.bat
rem needs: build\x64\Release\DPKViewer.exe (run tools\build_app.bat first)
rem output: tests\work\cli_out\, tests\work\cli_pack*.dpk, tests\work\log_cli_*.txt
setlocal
chcp 65001 >nul
set HERE=%~dp0
set ROOT=%HERE%..\..
set EXE=%ROOT%\build\x64\Release\DPKViewer.exe
set WORK=%ROOT%\tests\work
set PKG=%WORK%\packages

if not exist "%EXE%" (
  echo [error] %EXE% not found, run tools\build_app.bat first
  exit /b 1
)
if not exist "%PKG%\ref_flags0.dpk" (
  echo [error] reference packages missing, run tests\tools\build_and_test.bat first
  exit /b 1
)

if exist "%WORK%\cli_out" rmdir /s /q "%WORK%\cli_out"
if exist "%WORK%\cli_pack.dpk" del /q "%WORK%\cli_pack.dpk"
if exist "%WORK%\cli_pack_plain.dpk" del /q "%WORK%\cli_pack_plain.dpk"

echo === 1) /list ref_flags0.dpk ===
"%EXE%" /list "%PKG%\ref_flags0.dpk" /log "%WORK%\log_cli_list.txt"
echo exit=%errorlevel%

echo === 2) /extract ref_flags0.dpk -^> cli_out ===
"%EXE%" /extract "%PKG%\ref_flags0.dpk" "%WORK%\cli_out" /log "%WORK%\log_cli_extract.txt"
echo exit=%errorlevel%

echo === 3) /pack src -^> cli_pack.dpk (compress+encrypt, template ref_flags3) ===
"%EXE%" /pack "%WORK%\src" "%WORK%\cli_pack.dpk" /template "%PKG%\ref_flags3.dpk" /log "%WORK%\log_cli_pack.txt"
echo exit=%errorlevel%

echo === 4) /pack src -^> cli_pack_plain.dpk (stored, no encryption) ===
"%EXE%" /pack "%WORK%\src" "%WORK%\cli_pack_plain.dpk" /level 6 /nocompress /noencrypt /log "%WORK%\log_cli_pack_plain.txt"
echo exit=%errorlevel%

echo === 5) /? help ===
"%EXE%" /?
echo exit=%errorlevel%

endlocal
exit /b 0