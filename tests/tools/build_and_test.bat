@echo off
rem build_and_test.bat - compile the DPKViewer core modules against tests\TestCore.cpp
rem and cross-validate the result with the reference implementation (tests\tools\reference\dpk.py).
rem
rem usage: tests\tools\build_and_test.bat
rem output: tests\work\TestCore.exe, tests\work\log_core_test.txt
setlocal enabledelayedexpansion
chcp 65001 >nul
set HERE=%~dp0
set ROOT=%HERE%..\..
set WORK=%ROOT%\tests\work
if not exist "%WORK%" mkdir "%WORK%"
if not exist "%WORK%\obj" mkdir "%WORK%\obj"

rem ---- locate Visual Studio build tools ----
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (
  echo [error] vswhere.exe not found: "%VSWHERE%"
  exit /b 1
)
set VSPATH=
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
if "%VSPATH%"=="" (
  echo [error] no Visual Studio C++ toolset found
  exit /b 1
)
echo [info] toolset: %VSPATH%
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
rem NOTE: some VsDevCmd installs return non-zero (missing optional ext), so check cl.exe instead
where cl >nul 2>nul
if errorlevel 1 (
  echo [error] cl.exe not available after vcvars64.bat
  exit /b 1
)

rem ---- compile the core test console ----
set SRCS=
for %%F in ("%ROOT%\src\DPKViewer\Core\*.cpp") do set SRCS=!SRCS! "%%F"
echo [info] compiling TestCore.exe
cl /nologo /utf-8 /EHsc /std:c++17 /W3 /MT /O2 /D_CRT_SECURE_NO_WARNINGS ^
  /I "%ROOT%\src\DPKViewer" ^
  /Fe:"%WORK%\TestCore.exe" /Fo:"%WORK%\obj\\" ^
  "%ROOT%\tests\TestCore.cpp" !SRCS! > "%WORK%\log_core_build.txt" 2>&1
if errorlevel 1 (
  echo [error] compile failed, see "%WORK%\log_core_build.txt"
  type "%WORK%\log_core_build.txt"
  exit /b 1
)

rem ---- build test data with the reference implementation ----
echo [info] generating test data
python "%ROOT%\tests\tools\make_test_package.py" "%WORK%" > "%WORK%\log_make_data.txt" 2>&1
if errorlevel 1 (
  echo [error] make_test_package.py failed, see "%WORK%\log_make_data.txt"
  type "%WORK%\log_make_data.txt"
  exit /b 1
)

rem ---- run the core test ----
echo [info] running TestCore.exe
"%WORK%\TestCore.exe" "%WORK%" > "%WORK%\log_core_test.txt" 2>&1
set CORE_RC=%errorlevel%
type "%WORK%\log_core_test.txt"
if not "%CORE_RC%"=="0" (
  echo [error] TestCore failed with code %CORE_RC%
  exit /b 1
)

rem ---- cross-validate with the reference implementation ----
echo [info] cross-validating with reference dpk.py
python "%ROOT%\tests\tools\verify.py" "%WORK%" > "%WORK%\log_verify.txt" 2>&1
set VRC=%errorlevel%
type "%WORK%\log_verify.txt"
if not "%VRC%"=="0" (
  echo [error] verify.py failed with code %VRC%
  exit /b 1
)

echo ALL-TESTS: PASS
endlocal
exit /b 0