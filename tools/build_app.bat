@echo off
rem build_app.bat - build DPKViewer.exe (MFC) with MSBuild.
rem
rem usage: tools\build_app.bat [Debug|Release] [x64|Win32]
rem output: build\<Platform>\<Configuration>\DPKViewer.exe, build\log_msbuild.txt
setlocal enabledelayedexpansion
chcp 65001 >nul
set HERE=%~dp0
set ROOT=%~dp0..
set CFG=%~1
if "%CFG%"=="" set CFG=Release
set PLAT=%~2
if "%PLAT%"=="" set PLAT=x64
set LOG=%ROOT%\build\log_msbuild.txt
if not exist "%ROOT%\build" mkdir "%ROOT%\build"

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
where cl >nul 2>nul
if errorlevel 1 (
  echo [error] cl.exe not available after vcvars64.bat
  exit /b 1
)

rem ---- build the MFC application ----
echo [info] msbuild %CFG%^|%PLAT%
msbuild "%ROOT%\DPKViewer.sln" /nologo /m /t:Build /p:Configuration=%CFG% /p:Platform=%PLAT% > "%LOG%" 2>&1
set RC=%errorlevel%
type "%LOG%"
if not "%RC%"=="0" (
  echo [error] msbuild failed with code %RC%, see "%LOG%"
  exit /b %RC%
)
set EXE=%ROOT%\build\%PLAT%\%CFG%\DPKViewer.exe
if not exist "%EXE%" (
  echo [error] %EXE% not produced
  exit /b 1
)
echo BUILD-OK: "%EXE%"
endlocal
exit /b 0