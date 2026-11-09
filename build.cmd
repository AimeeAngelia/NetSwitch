@echo off
setlocal
cd /d "%~dp0"
if defined VSCMD_VER goto ready
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio C++ Build Tools are required.
  exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT (
  echo Visual Studio C++ Build Tools were not found.
  exit /b 1
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
:ready
if not exist build mkdir build
pushd build
rc.exe /nologo /c65001 /I .. /fo app.res ..\app.rc
if errorlevel 1 exit /b 1
cl.exe /nologo /std:c++20 /utf-8 /O2 /MT /EHsc /W4 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /I .. ..\main.cpp ..\core.cpp ..\service.cpp ..\ui.cpp ..\tests.cpp app.res /link /SUBSYSTEM:WINDOWS /OUT:NetSwitch.exe /MANIFEST:NO /Brepro wbemuuid.lib iphlpapi.lib ws2_32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib user32.lib gdi32.lib gdiplus.lib dwmapi.lib comctl32.lib
set "BUILD_RESULT=%errorlevel%"
popd
exit /b %BUILD_RESULT%
