@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

if /I "%~1"=="--help" goto :help
if /I "%~1"=="-h" goto :help

for %%I in ("%~dp0.") do set "SOURCE_DIR=%%~fI"
set "BUILD_DIR=%~dp0build"
set "OBJECT_DIR=%~dp0build\obj"
set "RUN_AFTER_BUILD=0"

if /I "%~1"=="run" set "RUN_AFTER_BUILD=1"
if /I not "%~1"=="" if /I not "%~1"=="run" (
    echo [ERROR] Unknown argument: %~1
    echo.
    goto :help_error
)

if defined QTDIR (
    set "QT_ROOT=%QTDIR%"
) else if defined QT_DIR (
    set "QT_ROOT=%QT_DIR%"
) else (
    set "QT_ROOT=C:\Qt\6.10.2\msvc2022_64"
)

if not exist "%QT_ROOT%\bin\windeployqt.exe" (
    echo [ERROR] Qt MSVC was not found at:
    echo        %QT_ROOT%
    echo Set QTDIR before running this file.
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe was not found. Install Visual Studio 2022 with C++.
    exit /b 1
)

set "VS_INSTALL_DIR="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL_DIR=%%I"

if not defined VS_INSTALL_DIR (
    echo [ERROR] Visual Studio 2022 with C++ tools was not found.
    exit /b 1
)

call "%VS_INSTALL_DIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [ERROR] The MSVC compiler environment could not be initialized.
    exit /b 1
)

set "CMAKE_EXE=%VS_INSTALL_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA_DIR=%VS_INSTALL_DIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"

if not exist "%CMAKE_EXE%" (
    for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE_EXE_FOUND set "CMAKE_EXE_FOUND=%%I"
    set "CMAKE_EXE=!CMAKE_EXE_FOUND!"
)

if not exist "%CMAKE_EXE%" (
    echo [ERROR] CMake was not found.
    exit /b 1
)

if exist "%NINJA_DIR%\ninja.exe" set "PATH=%NINJA_DIR%;%PATH%"
where ninja.exe >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Ninja was not found. Install the Visual Studio CMake component.
    exit /b 1
)

if not defined MILLERSCORE_JOBS set "MILLERSCORE_JOBS=%NUMBER_OF_PROCESSORS%"
if not defined MILLERSCORE_JOBS set "MILLERSCORE_JOBS=4"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if errorlevel 1 exit /b 1

echo.
echo ============================================================
echo  MillerScore - local build
echo  Objects:    %OBJECT_DIR%
echo  Executable: %BUILD_DIR%\millerscore.exe
echo  Qt:         %QT_ROOT%
echo ============================================================
echo.

echo [1/5] Configuring CMake...
"%CMAKE_EXE%" -S "%SOURCE_DIR%" -B "%OBJECT_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    -DCMAKE_INSTALL_PREFIX="%BUILD_DIR%" ^
    -DCMAKE_PREFIX_PATH="%QT_ROOT%" ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
    -DMUE_RUN_WINDEPLOYQT=OFF ^
    -DMUSE_COMPILE_USE_PCH=ON ^
    -DMUSE_COMPILE_USE_UNITY=OFF ^
    -DMUSE_COMPILE_USE_COMPILER_CACHE=OFF ^
    -DMUSE_ENABLE_UNIT_TESTS=OFF ^
    -DMUSE_MODULE_AUDIO_ASIO=ON ^
    -DMUSE_MODULE_VST=ON ^
    -DCMAKE_NINJA_CMCLDEPS_RC=OFF ^
    -DBUILD_TESTING=OFF ^
    -DKDDockWidgets_EXAMPLES=OFF ^
    -DMUE_BUILD_BRAILLE_TESTS=OFF ^
    -DMUE_BUILD_CONVERTER_TESTS=OFF ^
    -DMUE_BUILD_ENGRAVING_TESTS=OFF ^
    -DMUE_BUILD_IMPORTEXPORT_TESTS=OFF ^
    -DMUE_BUILD_NOTATION_TESTS=OFF ^
    -DMUE_BUILD_NOTATIONSCENE_TESTS=OFF ^
    -DMUE_BUILD_PLAYBACK_TESTS=OFF ^
    -DMUE_BUILD_PROJECT_TESTS=OFF
if errorlevel 1 goto :build_failed

echo [2/5] Building MillerScore...
"%CMAKE_EXE%" --build "%OBJECT_DIR%" --target MuseScoreStudio translations museupdater --parallel %MILLERSCORE_JOBS%
if errorlevel 1 goto :build_failed

echo [3/5] Installing resources into build\...
"%CMAKE_EXE%" --install "%OBJECT_DIR%"
if errorlevel 1 goto :build_failed

set "REAL_EXE=%BUILD_DIR%\bin\MillerScore.exe"
if not exist "%REAL_EXE%" (
    echo [ERROR] The build finished without producing:
    echo        %REAL_EXE%
    exit /b 1
)

echo [4/5] Deploying Qt dependencies...
"%QT_ROOT%\bin\windeployqt.exe" --dir "%BUILD_DIR%" --libdir "%BUILD_DIR%\bin" --plugindir "%BUILD_DIR%\plugins" --no-translations "%REAL_EXE%"
if errorlevel 1 goto :build_failed

echo [5/5] Creating build\millerscore.exe...
cl.exe /nologo /O2 /EHsc /DUNICODE /D_UNICODE ^
    /Fe:"%BUILD_DIR%\millerscore.exe" ^
    "%SOURCE_DIR%\buildscripts\tools\millerscore_launcher.cpp" ^
    user32.lib ^
    /link /SUBSYSTEM:WINDOWS
if errorlevel 1 goto :build_failed

echo.
echo ============================================================
echo  BUILD COMPLETE
echo  Open: %BUILD_DIR%\millerscore.exe
echo ============================================================
echo.

if "%RUN_AFTER_BUILD%"=="1" start "" "%BUILD_DIR%\millerscore.exe"
exit /b 0

:build_failed
echo.
echo [ERROR] The build failed. Review the messages above.
exit /b 1

:help
echo Usage:
echo   build_millerscore.bat       Build the application
echo   build_millerscore.bat run   Build and open the application
echo.
echo Output:
echo   build\millerscore.exe
echo.
echo Optional variables:
echo   QTDIR=C:\path\to\Qt\msvc2022_64
echo   MILLERSCORE_JOBS=number_of_jobs
exit /b 0

:help_error
echo Usage: build_millerscore.bat [run]
exit /b 2
