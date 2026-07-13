@echo off
setlocal
cd /d "%~dp0"

set "CONFIGURATION=Debug"
set "PLATFORM=x64"
set "PROJECT=%~dp0Server.vcxproj"
set "EXE=%~dp0%PLATFORM%\%CONFIGURATION%\Server.exe"
set "VCVARS="

set "MSBUILD="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -version [17.0^,18.0^) -requires Microsoft.Component.MSBuild -find MSBuild\Current\Bin\MSBuild.exe`) do (
        set "MSBUILD=%%I"
    )
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -version [17.0^,18.0^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat`) do (
        set "VCVARS=%%I"
    )
)

if not defined MSBUILD (
    for %%I in (
        "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
        "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    ) do (
        if exist %%~I set "MSBUILD=%%~I"
    )
)

if not defined VCVARS (
    for %%I in (
        "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
        "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    ) do (
        if exist %%~I set "VCVARS=%%~I"
    )
)

if not defined MSBUILD (
    echo Visual Studio 2022 MSBuild was not found.
    echo Please install VS2022 with Desktop development with C++.
    pause
    exit /b 1
)

if not defined VCVARS (
    echo Visual Studio 2022 C++ build environment was not found.
    echo Please install VS2022 with Desktop development with C++.
    pause
    exit /b 1
)

echo Building Server with Visual Studio 2022...
call "%VCVARS%" >nul
"%MSBUILD%" "%PROJECT%" /m /p:Configuration=%CONFIGURATION% /p:Platform=%PLATFORM%
if errorlevel 1 (
    echo Build failed.
    pause
    exit /b 1
)

echo Build succeeded.
echo Running Server...
chcp 65001 >nul
"%EXE%"
pause
