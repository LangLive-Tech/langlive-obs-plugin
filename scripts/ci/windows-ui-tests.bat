@echo off
rem Builds and runs the UI tests (the Qt dialogs, headless) with Visual
rem Studio. Needs .deps/ from a plugin configure (Qt 6, obs-studio headers),
rem so CI runs it after the Windows build. Also runs locally from a plain
rem command prompt.
setlocal EnableExtensions

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: vswhere.exe not found; install Visual Studio with the C++ workload
  exit /b 1
)
set "VS_DIR="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -all -products * -property installationPath`) do (
  if not defined VS_DIR if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VS_DIR=%%i"
)
if not defined VS_DIR (
  echo ERROR: no Visual Studio with the C++ x64 tools
  exit /b 1
)
echo Visual Studio: %VS_DIR%
set "PATH=%VS_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"

rem The Visual Studio generator, like the plugin build: OBS's Qt is built for it.
cmake -S tests/ui -B build_ui_tests -A x64 || exit /b 1
cmake --build build_ui_tests --config RelWithDebInfo || exit /b 1
ctest --test-dir build_ui_tests -C RelWithDebInfo --output-on-failure --timeout 300 || exit /b 1
endlocal
