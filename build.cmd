@echo off
setlocal enableextensions
set "ROOT=%~dp0"
set "VSVC="
if defined VSINSTALLDIR if exist "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvarsall.bat" set "VSVC=%VSINSTALLDIR%\VC\Auxiliary\Build\vcvarsall.bat"
if defined VSVC goto :toolchain
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio 2022 C++ x64 Build Tools required. 1>&2
  exit /b 1
)
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSVC=%%I\VC\Auxiliary\Build\vcvarsall.bat"
if not defined VSVC exit /b 1
:toolchain
call "%VSVC%" x64 >nul
if errorlevel 1 exit /b 1
if not exist "%ROOT%build" mkdir "%ROOT%build"
if /I "%~1"=="test" goto :test
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE /LD "%ROOT%src\plugin.cpp" /Fe:"%ROOT%build\CapitalizeNext.dll" /Fo:"%ROOT%build\plugin.obj" /link user32.lib comctl32.lib
if errorlevel 1 exit /b 1
echo Built: %ROOT%build\CapitalizeNext.dll
exit /b 0
:test
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /utf-8 "%ROOT%tests\rules_test.cpp" /Fe:"%ROOT%build\rules_test.exe" /Fo:"%ROOT%build\rules_test.obj"
if errorlevel 1 exit /b 1
"%ROOT%build\rules_test.exe"
exit /b %errorlevel%
