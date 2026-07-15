@echo off
cd /d "%~dp0"
set "APP=%~dp0..\x64\Release\Project3.exe"

if not exist "%APP%" (
    echo Release executable was not found:
    echo %APP%
    echo Build Release x64 in Visual Studio first.
    pause
    exit /b 1
)

echo Campus News Hub update is running. Please keep this window open.
echo.
"%APP%"
set "EXIT_CODE=%ERRORLEVEL%"
echo.
echo Program finished with exit code %EXIT_CODE%.
pause
exit /b %EXIT_CODE%

