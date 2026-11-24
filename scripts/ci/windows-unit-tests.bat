@echo off
rem Builds and runs the unit tests with MSVC (warnings as errors). Used by CI;
rem also runs locally from a plain command prompt.
setlocal EnableExtensions

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: vswhere.exe not found; install Visual Studio with the C++ workload
  exit /b 1
)
rem Any Visual Studio edition (or Build Tools) whose vcvars64.bat exists; the
rem C++ component IDs differ between editions, so check for the file instead.
set "VS_DIR="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -all -products * -property installationPath`) do (
  if not defined VS_DIR if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VS_DIR=%%i"
)
if not defined VS_DIR (
  echo ERROR: no Visual Studio with the C++ x64 tools. vswhere found:
  "%VSWHERE%" -all -products * -property installationPath
  exit /b 1
)
echo Visual Studio: %VS_DIR%

call "%VS_DIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%VS_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

cmake -S tests -B build_tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_COMPILE_WARNING_AS_ERROR=ON || exit /b 1
cmake --build build_tests || exit /b 1
ctest --test-dir build_tests --output-on-failure || exit /b 1
endlocal
