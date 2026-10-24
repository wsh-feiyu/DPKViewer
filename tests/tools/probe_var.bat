@echo off
setlocal
set HERE=%~dp0
set ROOT=%HERE%..\..
set WORK=%ROOT%\tests\work
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
set VSPATH=
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /nologo /utf-8 /EHsc /std:c++17 /W3 /MT /O2 /D_CRT_SECURE_NO_WARNINGS /I "%ROOT%\src\DPKViewer" /Fe:"%WORK%\VarProbe.exe" /Fo:"%WORK%\obj\\" "%ROOT%\tests\VarProbe.cpp" "%ROOT%\src\DPKViewer\Core\Des.cpp" "%ROOT%\src\DPKViewer\Core\FileIO.cpp" "%ROOT%\src\DPKViewer\Core\StrUtil.cpp"
if errorlevel 1 exit /b 1
"%WORK%\VarProbe.exe" "%WORK%\des_fixtures.txt"
