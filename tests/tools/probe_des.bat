@echo off
setlocal
set HERE=%~dp0
set ROOT=%HERE%..\..
set WORK=%ROOT%\tests\work
if not exist "%WORK%" mkdir "%WORK%"
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
set VSPATH=
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
where cl >nul 2>nul
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /EHsc /std:c++17 /W3 /MT /O2 /D_CRT_SECURE_NO_WARNINGS /I "%ROOT%\src\DPKViewer" /Fe:"%WORK%\DesProbe.exe" /Fo:"%WORK%\obj\\" "%ROOT%\tests\DesProbe.cpp" "%ROOT%\src\DPKViewer\Core\Des.cpp"
if errorlevel 1 exit /b 1
"%WORK%\DesProbe.exe"
